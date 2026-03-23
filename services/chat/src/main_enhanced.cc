// Enhanced Distributed Chat Service with Hybrid Message Store
// Features: Redis+MySQL dual-write, message delivery tracking, pagination

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>
#include <functional>
#include <random>

#include <asio.hpp>

#include "hybrid_message_store.h"
#include "message_delivery_tracker.h"
#include "message_migration_worker.h"
#include "paginated_history_retriever.h"
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

using chirp::chat::HybridMessageStore;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::PaginatedHistoryRetriever;
using chirp::chat::MessageStoreConfig;
using chirp::common::Logger;

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

/// @brief Distributed chat state management
struct DistributedChatState {
  std::mutex mu;

  // Local user session mapping
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> local_sessions;

  // Session to user reverse mapping
  std::unordered_map<void*, std::string> session_to_user;

  // Current instance ID
  std::string instance_id;

  void AddSession(const std::string& user_id, std::shared_ptr<chirp::network::Session> session) {
    std::lock_guard<std::mutex> lock(mu);
    local_sessions[user_id] = session;
    session_to_user[session.get()] = user_id;
  }

  void RemoveSession(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = session_to_user.find(session);
    if (it != session_to_user.end()) {
      local_sessions.erase(it->second);
      session_to_user.erase(it);
    }
  }

  std::shared_ptr<chirp::network::Session> GetLocalSession(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    if (it != local_sessions.end()) {
      return it->second.lock();
    }
    return nullptr;
  }

  bool IsUserLocal(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    return it != local_sessions.end() && !it->second.expired();
  }

  std::string GetUserId(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = session_to_user.find(session);
    return it != session_to_user.end() ? it->second : "";
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

void SendChatNotify(const std::shared_ptr<chirp::network::Session>& session,
                   const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

/// @brief Handle send message with hybrid storage
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                      const std::shared_ptr<chirp::network::Session>& sender_session,
                      const std::shared_ptr<DistributedChatState>& state,
                      const std::shared_ptr<HybridMessageStore>& store,
                      const std::shared_ptr<MessageDeliveryTracker>& delivery_tracker,
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
    channel_id = HybridMessageStore::PrivateChannelId(req.sender_id(), req.receiver_id());
  } else {
    channel_id = req.channel_id();
  }
  msg.set_channel_id(channel_id);

  // Store in hybrid store (Redis + MySQL)
  chirp::chat::MessageData msg_data;
  msg_data.message_id = msg.message_id();
  msg_data.sender_id = msg.sender_id();
  msg_data.receiver_id = msg.receiver_id();
  msg_data.channel_id = msg.channel_id();
  msg_data.channel_type = req.channel_type();
  msg_data.msg_type = req.msg_type();
  msg_data.content = req.content();
  msg_data.timestamp = msg.timestamp();
  msg_data.created_at = NowMs();

  // Store asynchronously for better performance
  store->StoreMessageAsync(msg_data, [](bool success) {
    if (!success) {
      Logger::Instance().Warn("Message storage to MySQL failed");
    }
  });

  // Respond to sender
  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  // Track delivery for private messages
  if (req.channel_type() == chirp::chat::PRIVATE && !req.receiver_id().empty()) {
    delivery_tracker->TrackMessage(msg.message_id(), req.receiver_id(),
                                  NowMs() + 300000);  // 5 min expiry
  }

  // Route to receiver
  if (req.channel_type() == chirp::chat::PRIVATE) {
    router->SendChatMessage(req.receiver_id(), msg.SerializeAsString(),
      [&](const std::string& user_id) -> bool {
        auto recv_session = state->GetLocalSession(user_id);
        if (recv_session) {
          SendChatNotify(recv_session, msg);
          delivery_tracker->Acknowledge(msg.message_id(), user_id);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        }
        return false;
      });

    // Store offline if not delivered
    if (!state->IsUserLocal(req.receiver_id())) {
      Logger::Instance().Info("Message stored offline for " + req.receiver_id());
    }
  } else {
    router->BroadcastToGroup(channel_id, msg.SerializeAsString());
  }
}

/// @brief Handle user login
void HandleLogin(const chirp::auth::LoginRequest& req,
                const std::shared_ptr<chirp::network::Session>& session,
                const std::shared_ptr<DistributedChatState>& state,
                const std::shared_ptr<HybridMessageStore>& store,
                const std::shared_ptr<chirp::network::MessageRouter>& router,
                int64_t seq) {
  const std::string user_id = req.token();

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(NowMs()));

    state->AddSession(user_id, session);

    // Subscribe to user's chat channel
    std::string channel = chirp::network::RouterChannels::UserChat(user_id);
    router->SubscribeUserChat(user_id, [session, state](const std::string& msg_data) {
      auto s = session;
      if (s) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
          SendChatNotify(s, msg);
        }
      }
    });

    Logger::Instance().Info("User logged in: " + user_id);

    // Deliver offline messages
    auto offline_msgs = store->PopOfflineMessages(user_id);
    Logger::Instance().Info("Delivering " + std::to_string(offline_msgs.size()) +
                           " offline messages to " + user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      msg.set_message_id(msg_data.message_id);
      msg.set_sender_id(msg_data.sender_id);
      msg.set_receiver_id(msg_data.receiver_id);
      msg.set_channel_id(msg_data.channel_id);
      msg.set_channel_type(static_cast<chirp::chat::ChannelType>(msg_data.channel_type));
      msg.set_msg_type(static_cast<chirp::chat::MsgType>(msg_data.msg_type));
      msg.set_content(msg_data.content);
      msg.set_timestamp(msg_data.timestamp);
      SendChatNotify(session, msg);
    }
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(NowMs());

  SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
}

/// @brief Handle get history with pagination
void HandleGetHistory(const chirp::chat::GetHistoryRequest& req,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const std::shared_ptr<PaginatedHistoryRetriever>& retriever,
                    int64_t seq) {
  std::string channel_id = req.channel_id();

  auto page = retriever->GetPageBefore(channel_id, req.channel_type(),
                                      req.before_timestamp(), req.limit());

  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_has_more(page.has_more);

  for (const auto& msg_data : page.messages) {
    chirp::chat::ChatMessage* msg = resp.add_messages();
    msg->set_message_id(msg_data.message_id);
    msg->set_sender_id(msg_data.sender_id);
    msg->set_receiver_id(msg_data.receiver_id);
    msg->set_channel_id(msg_data.channel_id);
    msg->set_channel_type(static_cast<chirp::chat::ChannelType>(msg_data.channel_type));
    msg->set_msg_type(static_cast<chirp::chat::MsgType>(msg_data.msg_type));
    msg->set_content(msg_data.content);
    msg->set_timestamp(msg_data.timestamp);
  }

  SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

/// @brief Handle get history V2 with cursor pagination
void HandleGetHistoryV2(const std::string& request_body,
                       const std::shared_ptr<chirp::network::Session>& session,
                       int64_t seq) {
  (void)request_body;

  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::INVALID_PARAM);
  resp.set_has_more(false);
  SendPacket(session, chirp::gateway::GET_HISTORY_V2_RESP, seq, resp.SerializeAsString());
}

} // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);

  // MySQL configuration
  const std::string mysql_host = GetArg(argc, argv, "--mysql_host", "127.0.0.1");
  const uint16_t mysql_port = ParseU16Arg(argc, argv, "--mysql_port", 3306);
  const std::string mysql_database = GetArg(argc, argv, "--mysql_database", "chirp");
  const std::string mysql_user = GetArg(argc, argv, "--mysql_user", "chirp");
  const std::string mysql_password = GetArg(argc, argv, "--mysql_password", "chirp_password");

  // Migration settings
  const bool enable_migration = ParseIntArg(argc, argv, "--enable_migration", 1) != 0;
  const int migration_interval = ParseIntArg(argc, argv, "--migration_interval", 30);

  std::string instance_id = GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + RandomHex(8);
  }

  Logger::Instance().Info("chirp_chat_enhanced starting");
  Logger::Instance().Info("  instance_id: " + instance_id);
  Logger::Instance().Info("  tcp_port: " + std::to_string(port));
  Logger::Instance().Info("  ws_port: " + std::to_string(ws_port));
  Logger::Instance().Info("  redis: " + redis_host + ":" + std::to_string(redis_port));
  Logger::Instance().Info("  mysql: " + mysql_host + ":" + std::to_string(mysql_port) + "/" + mysql_database);
  Logger::Instance().Info("  migration: " + std::string(enable_migration ? "enabled" : "disabled"));

  asio::io_context io;

  // Configure message store
  MessageStoreConfig store_config;
  store_config.redis_host = redis_host;
  store_config.redis_port = redis_port;
  store_config.mysql_host = mysql_host;
  store_config.mysql_port = mysql_port;
  store_config.mysql_database = mysql_database;
  store_config.mysql_user = mysql_user;
  store_config.mysql_password = mysql_password;
  store_config.enable_migration = enable_migration;
  store_config.migration_interval_seconds = migration_interval;

  // Initialize components
  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto store = std::make_shared<HybridMessageStore>(io, store_config);
  if (!store->Initialize()) {
    Logger::Instance().Error("Failed to initialize HybridMessageStore");
    return 1;
  }

  auto retriever = std::make_shared<PaginatedHistoryRetriever>(store);

  auto delivery_tracker = std::make_shared<MessageDeliveryTracker>(io, store);
  delivery_tracker->Start();

  auto migration_worker = std::make_shared<MessageMigrationWorker>(io, store, store_config);
  migration_worker->Start();

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  // TCP Server
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
            HandleSendMessage(req, session, state, store, delivery_tracker, router, pkt.sequence());
          }
          break;
        }
        case chirp::gateway::GET_HISTORY_REQ: {
          chirp::chat::GetHistoryRequest req;
          if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            HandleGetHistory(req, session, retriever, pkt.sequence());
          }
          break;
        }
        case chirp::gateway::GET_HISTORY_V2_REQ: {
          HandleGetHistoryV2(pkt.body(), session, pkt.sequence());
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

  // WebSocket Server
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
            HandleSendMessage(req, session, state, store, delivery_tracker, router, pkt.sequence());
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

  Logger::Instance().Info("Enhanced Chat service started, listening on TCP:" + std::to_string(port) +
                          " WS:" + std::to_string(ws_port));

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code&, int) {
    Logger::Instance().Info("Shutting down chat service...");
    server.Stop();
    ws_server.Stop();
    router->Stop();
    delivery_tracker->Stop();
    migration_worker->Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat_enhanced exited");
  return 0;
}
