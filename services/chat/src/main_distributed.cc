#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>

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

std::string RandomHex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  static const char* kHex = "0123456789abcdef";

  std::string out;
  out.resize(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    uint8_t b = static_cast<uint8_t>(dist(rng));
    out[i * 2] = kHex[(b >> 4) & 0xF];
    out[i * 2 + 1] = kHex[b & 0xF];
  }
  return out;
}

std::string GenerateMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

/// @brief 分布式聊天状态管理
struct DistributedChatState {
  std::mutex mu;

  // 本地用户会话映射 (user_id -> session)
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> local_sessions;

  // 会话到用户的反向映射
  std::unordered_map<void*, std::string> session_to_user;

  // 当前实例ID
  std::string instance_id;

  /// @brief 添加本地会话
  void AddSession(const std::string& user_id, std::shared_ptr<chirp::network::Session> session) {
    std::lock_guard<std::mutex> lock(mu);
    local_sessions[user_id] = session;
    session_to_user[session.get()] = user_id;
  }

  /// @brief 移除会话
  void RemoveSession(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = session_to_user.find(session);
    if (it != session_to_user.end()) {
      local_sessions.erase(it->second);
      session_to_user.erase(it);
    }
  }

  /// @brief 获取本地会话
  std::shared_ptr<chirp::network::Session> GetLocalSession(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    if (it != local_sessions.end()) {
      return it->second.lock();
    }
    return nullptr;
  }

  /// @brief 检查用户是否在本地
  bool IsUserLocal(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    return it != local_sessions.end() && !it->second.expired();
  }

  /// @brief 获取用户ID（根据session）
  std::string GetUserId(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = session_to_user.find(session);
    return it != session_to_user.end() ? it->second : "";
  }
};

/// @brief 消息存储（使用 Redis）
struct DistributedMessageStore {
  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};

  std::string OfflineKey(const std::string& user_id) {
    return "chirp:chat:offline:" + user_id;
  }

  std::string HistoryKey(const std::string& channel_id) {
    return "chirp:chat:history:" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) {
    return a < b ? a + "|" + b : b + "|" + a;
  }

  /// @brief 添加离线消息到 Redis
  void AddOffline(const std::string& receiver_id, const std::string& message) {
    if (redis && !receiver_id.empty()) {
      redis->RPush(OfflineKey(receiver_id), message);
      redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
    }
  }

  /// @brief 获取并清空离线消息
  std::vector<std::string> PopOffline(const std::string& user_id) {
    if (!redis || user_id.empty()) {
      return {};
    }
    auto messages = redis->LRange(OfflineKey(user_id), 0, -1);
    redis->Del(OfflineKey(user_id));
    return messages;
  }

  /// @brief 保存消息到历史记录
  void AddToHistory(const std::string& channel_id, const std::string& message) {
    if (redis && !channel_id.empty()) {
      redis->RPush(HistoryKey(channel_id), message);
      redis->LTrim(HistoryKey(channel_id), -100, -1);  // 只保留最近100条
    }
  }

  /// @brief 获取历史消息
  std::vector<std::string> GetHistory(const std::string& channel_id, int limit) {
    if (!redis) {
      return {};
    }
    if (limit <= 0) limit = 50;
    return redis->LRange(HistoryKey(channel_id), -limit, -1);
  }
};

/// @brief 发送数据包到会话
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

/// @brief 发送聊天通知
void SendChatNotify(const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

/// @brief 处理私聊消息发送（分布式版本）
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& sender_session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<DistributedMessageStore>& store,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
                       int64_t seq) {
  using chirp::common::Logger;

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

  // 保存到历史
  store->AddToHistory(channel_id, msg.SerializeAsString());

  // 响应发送者
  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  // 智能路由消息到接收者
  if (req.channel_type() == chirp::chat::PRIVATE) {
    router->SendChatMessage(req.receiver_id(), msg.SerializeAsString(),
      [&](const std::string& user_id) -> bool {
        // 尝试本地投递
        auto recv_session = state->GetLocalSession(user_id);
        if (recv_session) {
          SendChatNotify(recv_session, msg);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        }
        return false;  // 本地投递失败，触发Redis转发
      });

    // 如果接收者不在线，保存离线消息
    if (!state->IsUserLocal(req.receiver_id())) {
      store->AddOffline(req.receiver_id(), msg.SerializeAsString());
      Logger::Instance().Info("Message stored offline for " + req.receiver_id());
    }
  } else {
    // 群组消息通过 Redis Pub/Sub 广播
    router->BroadcastToGroup(channel_id, msg.SerializeAsString());
  }
}

/// @brief 处理用户登录（分布式版本）
void HandleLogin(const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::shared_ptr<DistributedChatState>& state,
                 const std::shared_ptr<DistributedMessageStore>& store,
                 const std::shared_ptr<chirp::network::MessageRouter>& router,
                 int64_t seq) {
  using chirp::common::Logger;

  const std::string user_id = req.token();

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(NowMs()));

    // 添加到本地会话
    state->AddSession(user_id, session);

    // 订阅用户的聊天消息频道
    std::string channel = chirp::network::RouterChannels::UserChat(user_id);
    router->SubscribeUserChat(user_id, [session, state](const std::string& msg_data) {
      auto s = session;  // 捕获 shared_ptr 保持连接
      if (s) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
          SendChatNotify(s, msg);
        }
      }
    });

    Logger::Instance().Info("User logged in: " + user_id + " on instance " + state->instance_id);

    // 发送离线消息
    auto offline_msgs = store->PopOffline(user_id);
    Logger::Instance().Info("Delivering " + std::to_string(offline_msgs.size()) + " offline messages to " + user_id);
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

/// @brief 处理获取历史消息
void HandleGetHistory(const chirp::chat::GetHistoryRequest& req,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const std::shared_ptr<DistributedChatState>& state,
                      const std::shared_ptr<DistributedMessageStore>& store,
                      int64_t seq) {
  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::OK);

  std::string channel_id = req.channel_id();
  if (req.channel_type() == chirp::chat::PRIVATE) {
    std::string user_id = state->GetUserId(
      reinterpret_cast<chirp::network::Session*>(0x1));  // Hack: need to pass session
    // For private chat, channel_id is sender|receiver
    // We'll use the provided channel_id for now
  }

  auto history_data = store->GetHistory(channel_id, req.limit());
  for (const auto& msg_data : history_data) {
    chirp::chat::ChatMessage* msg = resp.add_messages();
    if (!msg->ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
      // Skip invalid messages
      resp.mutable_messages()->RemoveLast();
    }
  }

  resp.set_has_more(false);  // Simplified
  SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

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

  // 初始化组件
  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto redis_client = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  auto store = std::make_shared<DistributedMessageStore>();
  store->redis = redis_client;
  store->offline_ttl_seconds = offline_ttl;

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  // TCP 服务器
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
            HandleGetHistory(req, session, state, store, pkt.sequence());
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
          SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(),
                     pong.SerializeAsString());
          break;
        }
        default:
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        std::string user_id = state->GetUserId(session.get());
        if (!user_id.empty()) {
          Logger::Instance().Info("User disconnected: " + user_id);
        }
        state->RemoveSession(session.get());
      });

  // WebSocket 服务器
  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        // 处理逻辑与 TCP 相同
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
          SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(),
                     pong.SerializeAsString());
          break;
        }
        default:
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        state->RemoveSession(session.get());
      });

  server.Start();
  ws_server.Start();

  Logger::Instance().Info("Chat service started, listening on TCP:" + std::to_string(port) +
                          " WS:" + std::to_string(ws_port));

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
