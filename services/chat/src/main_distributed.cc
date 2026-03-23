#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "logger.h"
#include "network/message_router.h"
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

using chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; ++i) {
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

std::string RandomHex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  static const char* kHex = "0123456789abcdef";

  std::string out(bytes * 2, '\0');
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t b = static_cast<uint8_t>(dist(rng));
    out[i * 2] = kHex[(b >> 4) & 0x0F];
    out[i * 2 + 1] = kHex[b & 0x0F];
  }
  return out;
}

std::string GenerateMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

struct DistributedChatState {
  void AddSession(const std::string& user_id, const std::shared_ptr<chirp::network::Session>& session) {
    std::lock_guard<std::mutex> lock(mu);
    local_sessions[user_id] = session;
    session_to_user[session.get()] = user_id;
  }

  void RemoveSession(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = session_to_user.find(session);
    if (it == session_to_user.end()) {
      return;
    }
    local_sessions.erase(it->second);
    session_to_user.erase(it);
  }

  std::shared_ptr<chirp::network::Session> GetLocalSession(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = local_sessions.find(user_id);
    if (it == local_sessions.end()) {
      return nullptr;
    }
    return it->second.lock();
  }

  bool IsUserLocal(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = local_sessions.find(user_id);
    return it != local_sessions.end() && !it->second.expired();
  }

  std::string GetUserId(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = session_to_user.find(session);
    return it != session_to_user.end() ? it->second : "";
  }

  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> local_sessions;
  std::unordered_map<void*, std::string> session_to_user;
  std::string instance_id;
};

struct DistributedMessageStore {
  std::string OfflineKey(const std::string& user_id) const {
    return "chirp:chat:offline:" + user_id;
  }

  std::string HistoryKey(const std::string& channel_id) const {
    return "chirp:chat:history:" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) const {
    return a < b ? a + "|" + b : b + "|" + a;
  }

  void AddOffline(const std::string& receiver_id, const std::string& message) const {
    if (!redis || receiver_id.empty()) {
      return;
    }
    redis->RPush(OfflineKey(receiver_id), message);
    redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
  }

  std::vector<std::string> PopOffline(const std::string& user_id) const {
    if (!redis || user_id.empty()) {
      return {};
    }
    auto messages = redis->LRange(OfflineKey(user_id), 0, -1);
    redis->Del(OfflineKey(user_id));
    return messages;
  }

  void AddToHistory(const std::string& channel_id, const std::string& message) const {
    if (!redis || channel_id.empty()) {
      return;
    }
    redis->RPush(HistoryKey(channel_id), message);
  }

  std::vector<std::string> GetHistory(const std::string& channel_id, int limit) const {
    if (!redis) {
      return {};
    }
    if (limit <= 0) {
      limit = 50;
    }
    return redis->LRange(HistoryKey(channel_id), -limit, -1);
  }

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  const auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void SendChatNotify(const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());
  const auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& sender_session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<DistributedMessageStore>& store,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
                       int64_t seq) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(req.channel_type());
  msg.set_msg_type(req.msg_type());
  msg.set_content(req.content());
  msg.set_timestamp(NowMs());

  std::string channel_id;
  if (req.channel_type() == chirp::chat::PRIVATE) {
    if (req.receiver_id().empty()) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(NowMs());
      SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());
      return;
    }
    channel_id = store->PrivateChannelId(req.sender_id(), req.receiver_id());
  } else {
    channel_id = req.channel_id();
  }
  msg.set_channel_id(channel_id);

  store->AddToHistory(channel_id, msg.SerializeAsString());

  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  if (req.channel_type() == chirp::chat::PRIVATE) {
    router->SendChatMessage(
        req.receiver_id(), msg.SerializeAsString(), [&](const std::string& user_id) -> bool {
          auto recv_session = state->GetLocalSession(user_id);
          if (!recv_session) {
            return false;
          }
          SendChatNotify(recv_session, msg);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        });

    if (!state->IsUserLocal(req.receiver_id())) {
      store->AddOffline(req.receiver_id(), msg.SerializeAsString());
      Logger::Instance().Info("Message stored offline for " + req.receiver_id());
    }
  } else {
    router->BroadcastToGroup(channel_id, msg.SerializeAsString());
  }
}

void HandleLogin(const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::shared_ptr<DistributedChatState>& state,
                 const std::shared_ptr<DistributedMessageStore>& store,
                 const std::shared_ptr<chirp::network::MessageRouter>& router,
                 int64_t seq) {
  const std::string user_id = req.token();

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(NowMs()));

    state->AddSession(user_id, session);

    router->SubscribeUserChat(user_id, [session](const std::string& msg_data) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        SendChatNotify(session, msg);
      }
    });

    Logger::Instance().Info("User logged in: " + user_id + " on instance " + state->instance_id);

    const auto offline_msgs = store->PopOffline(user_id);
    Logger::Instance().Info(
        "Delivering " + std::to_string(offline_msgs.size()) + " offline messages to " + user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        SendChatNotify(session, msg);
      }
    }
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(NowMs());

  SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
}

void HandleGetHistory(const chirp::chat::GetHistoryRequest& req,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const std::shared_ptr<DistributedMessageStore>& store,
                      int64_t seq) {
  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::OK);

  const auto history_data = store->GetHistory(req.channel_id(), req.limit());
  for (const auto& msg_data : history_data) {
    chirp::chat::ChatMessage* msg = resp.add_messages();
    if (!msg->ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
      resp.mutable_messages()->RemoveLast();
    }
  }

  resp.set_has_more(false);
  SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

}  // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int offline_ttl = ParseIntArg(argc, argv, "--offline_ttl", 604800);

  std::string instance_id = GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + RandomHex(8);
  }

  Logger::Instance().Info("chirp_chat_distributed starting");
  Logger::Instance().Info("  instance_id: " + instance_id);
  Logger::Instance().Info("  tcp_port: " + std::to_string(port));
  Logger::Instance().Info("  ws_port: " + std::to_string(ws_port));
  Logger::Instance().Info("  redis: " + redis_host + ":" + std::to_string(redis_port));

  asio::io_context io;

  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto store = std::make_shared<DistributedMessageStore>();
  store->redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  store->offline_ttl_seconds = offline_ttl;

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  chirp::network::TcpServer server(
      io, port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("Failed to parse packet");
          return;
        }

        switch (pkt.msg_id()) {
          case chirp::gateway::LOGIN_REQ: {
            chirp::auth::LoginRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              HandleLogin(req, session, state, store, router, pkt.sequence());
            }
            break;
          }
          case chirp::gateway::SEND_MESSAGE_REQ: {
            chirp::chat::SendMessageRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              HandleSendMessage(req, session, state, store, router, pkt.sequence());
            }
            break;
          }
          case chirp::gateway::GET_HISTORY_REQ: {
            chirp::chat::GetHistoryRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              HandleGetHistory(req, session, store, pkt.sequence());
            }
            break;
          }
          case chirp::gateway::LOGOUT_REQ: {
            chirp::auth::LogoutRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              state->RemoveSession(session.get());
              chirp::auth::LogoutResponse resp;
              resp.set_code(chirp::common::OK);
              resp.set_server_time(NowMs());
              SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
            }
            break;
          }
          case chirp::gateway::HEARTBEAT_PING: {
            chirp::gateway::HeartbeatPong pong;
            pong.set_timestamp(NowMs());
            pong.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
            break;
          }
          default:
            break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        const std::string user_id = state->GetUserId(session.get());
        if (!user_id.empty()) {
          Logger::Instance().Info("User disconnected: " + user_id);
        }
        state->RemoveSession(session.get());
      });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          return;
        }

        switch (pkt.msg_id()) {
          case chirp::gateway::LOGIN_REQ: {
            chirp::auth::LoginRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              HandleLogin(req, session, state, store, router, pkt.sequence());
            }
            break;
          }
          case chirp::gateway::SEND_MESSAGE_REQ: {
            chirp::chat::SendMessageRequest req;
            if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
              HandleSendMessage(req, session, state, store, router, pkt.sequence());
            }
            break;
          }
          case chirp::gateway::HEARTBEAT_PING: {
            chirp::gateway::HeartbeatPong pong;
            pong.set_timestamp(NowMs());
            pong.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
            break;
          }
          default:
            break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) { state->RemoveSession(session.get()); });

  server.Start();
  ws_server.Start();

  Logger::Instance().Info(
      "Chat service started, listening on TCP:" + std::to_string(port) + " WS:" + std::to_string(ws_port));

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code&, int) {
    Logger::Instance().Info("Shutting down chat service...");
    server.Stop();
    ws_server.Stop();
    router->Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat_distributed exited");
  return 0;
}
