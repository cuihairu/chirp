#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint16_t ParsePort(int argc, char** argv) {
  uint16_t port = 7000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--port" || a == "-p") && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[i + 1]));
      i++;
    }
  }
  return port;
}

std::string GenerateMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

struct ChatState {
  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;
};

// 简单的内存消息存储 (生产环境应使用 Redis/MySQL)
struct MessageStore {
  // channel_key -> messages
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> history;

  std::string ChannelKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return std::to_string(static_cast<int>(type)) + ":" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) {
    if (a <= b) {
      return a + "|" + b;
    }
    return b + "|" + a;
  }

  void AddMessage(const chirp::chat::ChatMessage& msg) {
    std::string key = ChannelKey(msg.channel_type(), msg.channel_id());
    auto& msgs = history[key];
    msgs.push_back(msg);

    // 只保留最近 100 条消息
    if (msgs.size() > 100) {
      msgs.erase(msgs.begin());
    }
  }

  std::vector<chirp::chat::ChatMessage> GetHistory(chirp::chat::ChannelType type,
                                                   const std::string& channel_id,
                                                   int64_t before_timestamp,
                                                   int32_t limit,
                                                   bool* has_more) {
    if (has_more) {
      *has_more = false;
    }

    std::string key = ChannelKey(type, channel_id);
    auto it = history.find(key);
    if (it == history.end()) {
      return {};
    }

    const auto& all_msgs = it->second;
    std::vector<chirp::chat::ChatMessage> result;

    int64_t before = before_timestamp;
    if (before <= 0) {
      before = NowMs() + 1;
    }
    int32_t lim = limit;
    if (lim <= 0) {
      lim = 50;
    }

    for (auto rit = all_msgs.rbegin(); rit != all_msgs.rend(); ++rit) {
      if (rit->timestamp() >= before) {
        continue;
      }
      result.push_back(*rit);
      if (static_cast<int32_t>(result.size()) >= lim) {
        if (has_more) {
          *has_more = (rit + 1) != all_msgs.rend();
        }
        break;
      }
    }

    std::reverse(result.begin(), result.end());
    return result;
  }
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void SendChatNotify(const std::shared_ptr<chirp::network::Session>& session, const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void KickSession(const std::shared_ptr<chirp::network::Session>& session, const std::string& reason) {
  chirp::auth::KickNotify kick;
  kick.set_reason(reason.empty() ? "kicked" : reason);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(kick.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParsePort(argc, argv);
  Logger::Instance().Info("chirp_chat starting on port " + std::to_string(port));

  asio::io_context io;

  auto store = std::make_shared<MessageStore>();
  auto state = std::make_shared<ChatState>();

  chirp::network::TcpServer server(
      io, port,
      [store, state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("failed to parse Packet from client");
          return;
        }

        switch (pkt.msg_id()) {
        case chirp::gateway::LOGIN_REQ: {
          // Scaffolding login: treat token as user_id.
          chirp::auth::LoginRequest login_req;
          if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LoginResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }
          const std::string user_id = login_req.token();

          chirp::auth::LoginResponse login_resp;
          if (user_id.empty()) {
            login_resp.set_code(chirp::common::INVALID_PARAM);
          } else {
            login_resp.set_code(chirp::common::OK);
            login_resp.set_user_id(user_id);
            login_resp.set_session_id("chat_session_" + user_id);
            login_resp.set_kick_previous(true);
            login_resp.mutable_kick()->set_reason("login from another device");
          }
          login_resp.set_server_time(NowMs());

          if (!user_id.empty()) {
            std::shared_ptr<chirp::network::Session> old;
            {
              std::lock_guard<std::mutex> lock(state->mu);
              auto it = state->user_to_session.find(user_id);
              if (it != state->user_to_session.end()) {
                old = it->second.lock();
              }
              state->user_to_session[user_id] = session;
              state->session_to_user[session.get()] = user_id;
            }
            if (old && old.get() != session.get()) {
              KickSession(old, "login from another device");
            }
          }

          SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());
          break;
        }
        case chirp::gateway::LOGOUT_REQ: {
          chirp::auth::LogoutRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LogoutResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          chirp::auth::LogoutResponse resp;
          resp.set_code(chirp::common::OK);
          resp.set_server_time(NowMs());
          {
            std::lock_guard<std::mutex> lock(state->mu);
            auto it = state->session_to_user.find(session.get());
            if (it != state->session_to_user.end()) {
              const std::string user_id = it->second;
              state->session_to_user.erase(it);
              auto it2 = state->user_to_session.find(user_id);
              if (it2 != state->user_to_session.end()) {
                auto cur = it2->second.lock();
                if (!cur || cur.get() == session.get()) {
                  state->user_to_session.erase(it2);
                }
              }
            }
          }
          SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        case chirp::gateway::SEND_MESSAGE_REQ: {
          chirp::chat::SendMessageRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::chat::SendMessageResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_timestamp(NowMs());
            SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }
          if (req.sender_id().empty() ||
              (req.channel_type() == chirp::chat::PRIVATE && req.receiver_id().empty()) ||
              (req.channel_type() != chirp::chat::PRIVATE && req.channel_id().empty())) {
            chirp::chat::SendMessageResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_timestamp(NowMs());
            SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          chirp::chat::ChatMessage msg;
          msg.set_message_id(GenerateMessageId());
          msg.set_sender_id(req.sender_id());
          msg.set_receiver_id(req.receiver_id());
          msg.set_channel_type(req.channel_type());
          msg.set_msg_type(req.msg_type());
          msg.set_content(req.content());
          msg.set_timestamp(NowMs());
          if (req.channel_type() == chirp::chat::PRIVATE) {
            msg.set_channel_id(store->PrivateChannelId(req.sender_id(), req.receiver_id()));
          } else {
            msg.set_channel_id(req.channel_id());
          }

          store->AddMessage(msg);

          chirp::chat::SendMessageResponse resp;
          resp.set_code(chirp::common::OK);
          resp.set_message_id(msg.message_id());
          resp.set_server_timestamp(msg.timestamp());
          SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());

          if (req.channel_type() == chirp::chat::PRIVATE) {
            std::shared_ptr<chirp::network::Session> recv;
            {
              std::lock_guard<std::mutex> lock(state->mu);
              auto it = state->user_to_session.find(req.receiver_id());
              if (it != state->user_to_session.end()) {
                recv = it->second.lock();
              }
            }
            if (recv) {
              SendChatNotify(recv, msg);
            }
          }
          break;
        }
        case chirp::gateway::GET_HISTORY_REQ: {
          chirp::chat::GetHistoryRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::chat::GetHistoryResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_has_more(false);
            SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }
          if (req.channel_id().empty()) {
            chirp::chat::GetHistoryResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_has_more(false);
            SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          bool has_more = false;
          auto msgs =
              store->GetHistory(req.channel_type(), req.channel_id(), req.before_timestamp(), req.limit(), &has_more);

          chirp::chat::GetHistoryResponse resp;
          resp.set_code(chirp::common::OK);
          resp.set_has_more(has_more);
          for (auto& m : msgs) {
            *resp.add_messages() = std::move(m);
          }
          SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        default:
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        std::lock_guard<std::mutex> lock(state->mu);
        auto it = state->session_to_user.find(session.get());
        if (it == state->session_to_user.end()) {
          return;
        }
        const std::string user_id = it->second;
        state->session_to_user.erase(it);
        auto it2 = state->user_to_session.find(user_id);
        if (it2 != state->user_to_session.end()) {
          auto cur = it2->second.lock();
          if (!cur || cur.get() == session.get()) {
            state->user_to_session.erase(it2);
          }
        }
      });

  server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat exited");
  return 0;
}
