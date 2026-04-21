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

#include "chat_session_registry.h"
#include "chat_validation.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  return static_cast<uint16_t>(std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str()));
}

int ParseIntArg(int argc, char** argv, const std::string& key, int def) {
  return std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str());
}

std::string GenerateMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

std::string GenerateSessionId() {
  static std::atomic<uint64_t> counter{1};
  return "chat_session_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

struct MessageStore {
  static constexpr size_t kMaxHistory = 100;
  static constexpr size_t kMaxOfflineInMemory = 200;

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};

  // channel_key -> messages
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> history;
  // receiver_id -> pending messages (redis 不可用时兜底)
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> offline_messages;

  explicit MessageStore(std::shared_ptr<chirp::network::RedisClient> redis_client, int ttl)
      : redis(std::move(redis_client)), offline_ttl_seconds(ttl) {}

  std::string HistoryKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return "chat:history:" + ChannelKey(type, channel_id);
  }

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
    if (redis) {
      redis->RPush(HistoryKey(msg.channel_type(), msg.channel_id()), msg.SerializeAsString());
    }

    auto& msgs = history[ChannelKey(msg.channel_type(), msg.channel_id())];
    msgs.push_back(msg);

    // 只保留最近 100 条消息
    if (msgs.size() > kMaxHistory) {
      msgs.erase(msgs.begin());
    }
  }

  std::string OfflineKey(const std::string& user_id) { return "chat:offline:" + user_id; }

  void AddOffline(const std::string& receiver_id, const chirp::chat::ChatMessage& msg) {
    if (receiver_id.empty()) {
      return;
    }
    if (redis && redis->RPush(OfflineKey(receiver_id), msg.SerializeAsString())) {
      if (offline_ttl_seconds > 0) {
        redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
      }
      return;
    }

    auto& pending = offline_messages[receiver_id];
    pending.push_back(msg);
    if (pending.size() > kMaxOfflineInMemory) {
      pending.erase(pending.begin());
    }
  }

  std::vector<chirp::chat::ChatMessage> PopOffline(const std::string& user_id) {
    std::vector<chirp::chat::ChatMessage> out;
    if (user_id.empty()) {
      return out;
    }

    if (redis) {
      auto raw = redis->LRange(OfflineKey(user_id), 0, -1);
      if (!raw.empty()) {
        out.reserve(raw.size());
        for (const auto& item : raw) {
          chirp::chat::ChatMessage m;
          if (m.ParseFromArray(item.data(), static_cast<int>(item.size()))) {
            out.push_back(std::move(m));
          }
        }
      }
      redis->Del(OfflineKey(user_id));
      if (!out.empty()) {
        return out;
      }
    }

    auto it = offline_messages.find(user_id);
    if (it == offline_messages.end()) {
      return out;
    }
    out = std::move(it->second);
    offline_messages.erase(it);
    return out;
  }

  std::vector<chirp::chat::ChatMessage> GetHistory(chirp::chat::ChannelType type,
                                                   const std::string& channel_id,
                                                   int64_t before_timestamp,
                                                   int32_t limit,
                                                   bool* has_more) {
    if (has_more) {
      *has_more = false;
    }

    int64_t before = before_timestamp;
    if (before <= 0) {
      before = NowMs() + 1;
    }
    int32_t lim = limit;
    if (lim <= 0) {
      lim = 50;
    }

    if (redis) {
      auto raw = redis->LRange(HistoryKey(type, channel_id), 0, -1);
      if (!raw.empty()) {
        std::vector<chirp::chat::ChatMessage> result;
        result.reserve(raw.size());
        for (auto rit = raw.rbegin(); rit != raw.rend(); ++rit) {
          chirp::chat::ChatMessage msg;
          if (!msg.ParseFromArray(rit->data(), static_cast<int>(rit->size()))) {
            continue;
          }
          if (msg.timestamp() >= before) {
            continue;
          }
          result.push_back(std::move(msg));
          if (static_cast<int32_t>(result.size()) >= lim) {
            if (has_more) {
              *has_more = (rit + 1) != raw.rend();
            }
            break;
          }
        }
        std::reverse(result.begin(), result.end());
        return result;
      }
    }

    auto it = history.find(ChannelKey(type, channel_id));
    if (it == history.end()) {
      return {};
    }

    const auto& all_msgs = it->second;
    std::vector<chirp::chat::ChatMessage> result;
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

void SendPacketAndClose(const std::shared_ptr<chirp::network::Session>& session,
                        chirp::gateway::MsgID msg_id,
                        int64_t seq,
                        const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
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

void HandleDisconnect(const std::shared_ptr<chirp::chat::ChatState>& state,
                      const std::shared_ptr<chirp::network::Session>& session) {
  chirp::chat::RemoveAuthenticatedSession(state, session);
}

void HandlePacket(const std::shared_ptr<MessageStore>& store,
                  const std::shared_ptr<chirp::chat::ChatState>& state,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  const auto authenticated = chirp::chat::GetAuthenticatedSession(state, session);
  const std::string& authenticated_user_id = authenticated.user_id;
  const std::string& authenticated_session_id = authenticated.session_id;

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
      login_resp.set_session_id(GenerateSessionId());
      login_resp.set_kick_previous(true);
      login_resp.mutable_kick()->set_reason("login from another device");
    }
    login_resp.set_server_time(NowMs());

    if (!user_id.empty()) {
      auto old =
          chirp::chat::BindAuthenticatedSession(state, user_id, login_resp.session_id(), session);
      if (old && old.get() != session.get()) {
        KickSession(old, "login from another device");
      }
    }

    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());
    if (!user_id.empty()) {
      auto offline = store->PopOffline(user_id);
      for (const auto& m : offline) {
        SendChatNotify(session, m);
      }
    }
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
    resp.set_code(chirp::chat::ValidateLogoutRequest(req, authenticated_user_id, authenticated_session_id));
    resp.set_server_time(NowMs());
    if (resp.code() == chirp::common::OK) {
      HandleDisconnect(state, session);
      SendPacketAndClose(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
      break;
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
    const auto validation = chirp::chat::ValidateSendMessageRequest(req, authenticated_user_id);
    if (validation != chirp::common::OK) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(validation);
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
    resp.set_message_id(msg.message_id());
    resp.set_server_timestamp(msg.timestamp());

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
        resp.set_code(chirp::common::OK);
        SendChatNotify(recv, msg);
      } else {
        resp.set_code(chirp::common::TARGET_OFFLINE);
        store->AddOffline(req.receiver_id(), msg);
      }
    } else {
      resp.set_code(chirp::common::OK);
    }
    SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
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
    const auto validation = chirp::chat::ValidateGetHistoryRequest(req, authenticated_user_id);
    if (validation != chirp::common::OK) {
      chirp::chat::GetHistoryResponse resp;
      resp.set_code(validation);
      resp.set_has_more(false);
      SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    bool has_more = false;
    auto msgs = store->GetHistory(req.channel_type(), req.channel_id(), req.before_timestamp(), req.limit(), &has_more);

    chirp::chat::GetHistoryResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_has_more(has_more);
    for (auto& m : msgs) {
      *resp.add_messages() = std::move(m);
    }
    SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::HEARTBEAT_PING: {
    chirp::gateway::HeartbeatPing ping;
    if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      Logger::Instance().Warn("failed to parse HeartbeatPing body");
      return;
    }

    chirp::gateway::HeartbeatPong pong;
    pong.set_timestamp(ping.timestamp());
    pong.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    break;
  }
  default:
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int offline_ttl_seconds = ParseIntArg(argc, argv, "--offline_ttl", 604800);
  Logger::Instance().Info("chirp_chat starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty()
                               ? ""
                               : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                  " offline_ttl=" + std::to_string(offline_ttl_seconds))));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  auto store = std::make_shared<MessageStore>(redis, offline_ttl_seconds);
  auto state = std::make_shared<chirp::chat::ChatState>();

  chirp::network::TcpServer server(
      io, port,
      [store, state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [store, state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  server.Start();
  ws_server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat exited");
  return 0;
}
