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
#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "logger.h"
#include "network/message_router.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "runtime_utils.h"

namespace {

using chirp::chat::HybridMessageStore;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::PaginatedHistoryRetriever;
using chirp::chat::MessageStoreConfig;
using chirp::common::Logger;

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

/// @brief Handle send message with hybrid storage
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                      const std::shared_ptr<chirp::network::Session>& sender_session,
                      const std::shared_ptr<DistributedChatState>& state,
                      const std::shared_ptr<HybridMessageStore>& store,
                      const std::shared_ptr<MessageDeliveryTracker>& delivery_tracker,
                      const std::shared_ptr<chirp::network::MessageRouter>& router,
                      int64_t seq) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(req.channel_type());
  msg.set_msg_type(req.msg_type());
  msg.set_content(req.content());
  msg.set_timestamp(chirp::chat::runtime::NowMs());

  std::string channel_id;
  if (req.channel_type() == chirp::chat::PRIVATE) {
    if (req.receiver_id().empty()) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());
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
  msg_data.created_at = chirp::chat::runtime::NowMs();

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
  chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  // Track delivery for private messages
  if (req.channel_type() == chirp::chat::PRIVATE && !req.receiver_id().empty()) {
    delivery_tracker->TrackMessage(msg.message_id(), req.receiver_id(),
                                  chirp::chat::runtime::NowMs() + 300000);  // 5 min expiry
  }

  // Route to receiver
  if (req.channel_type() == chirp::chat::PRIVATE) {
    router->SendChatMessage(req.receiver_id(), msg.SerializeAsString(),
      [&](const std::string& user_id) -> bool {
        auto recv_session = state->GetLocalSession(user_id);
        if (recv_session) {
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
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
    resp.set_session_id(state->instance_id + "_" + std::to_string(chirp::chat::runtime::NowMs()));

    state->AddSession(user_id, session);

    // Subscribe to user's chat channel
    std::string channel = chirp::network::RouterChannels::UserChat(user_id);
    router->SubscribeUserChat(user_id, [session, state](const std::string& msg_data) {
      auto s = session;
      if (s) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
          chirp::chat::runtime::SendChatNotify(s, msg);
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
      chirp::chat::runtime::SendChatNotify(session, msg);
    }
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());

  chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
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

  chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

/// @brief Handle get history V2 with cursor pagination
void HandleGetHistoryV2(const std::string& request_body,
                       const std::shared_ptr<chirp::network::Session>& session,
                       int64_t seq) {
  (void)request_body;

  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::INVALID_PARAM);
  resp.set_has_more(false);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_V2_RESP, seq, resp.SerializeAsString());
}

} // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = chirp::chat::runtime::GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--redis_port", 6379);

  // MySQL configuration
  const std::string mysql_host = chirp::chat::runtime::GetArg(argc, argv, "--mysql_host", "127.0.0.1");
  const uint16_t mysql_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--mysql_port", 3306);
  const std::string mysql_database = chirp::chat::runtime::GetArg(argc, argv, "--mysql_database", "chirp");
  const std::string mysql_user = chirp::chat::runtime::GetArg(argc, argv, "--mysql_user", "chirp");
  const std::string mysql_password = chirp::chat::runtime::GetArg(argc, argv, "--mysql_password", "chirp_password");

  // Migration settings
  const bool enable_migration = chirp::chat::runtime::ParseIntArg(argc, argv, "--enable_migration", 1) != 0;
  const int migration_interval = chirp::chat::runtime::ParseIntArg(argc, argv, "--migration_interval", 30);

  std::string instance_id = chirp::chat::runtime::GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + chirp::chat::runtime::RandomHex(8);
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

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router](const std::shared_ptr<chirp::network::Session>& session,
                                             const chirp::auth::LoginRequest& req,
                                             int64_t seq) {
    HandleLogin(req, session, state, store, router, seq);
  };
  handlers.on_send_message = [state, store, delivery_tracker, router](
                                 const std::shared_ptr<chirp::network::Session>& session,
                                 const chirp::chat::SendMessageRequest& req,
                                 int64_t seq) {
    HandleSendMessage(req, session, state, store, delivery_tracker, router, seq);
  };
  handlers.on_get_history = [retriever](const std::shared_ptr<chirp::network::Session>& session,
                                        const chirp::chat::GetHistoryRequest& req,
                                        int64_t seq) {
    HandleGetHistory(req, session, retriever, seq);
  };
  handlers.on_get_history_v2 = [](const std::shared_ptr<chirp::network::Session>& session,
                                  const std::string& body,
                                  int64_t seq) {
    HandleGetHistoryV2(body, session, seq);
  };
  handlers.on_logout = [state](const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::auth::LogoutRequest&,
                               int64_t seq) {
    state->RemoveSession(session.get());
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };

  auto on_packet = [handlers](const std::shared_ptr<chirp::network::Session>& session,
                              const chirp::gateway::Packet& pkt) {
    chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  };

  auto tcp_disconnect = [state](const std::shared_ptr<chirp::network::Session>& session) {
    std::string user_id = state->GetUserId(session.get());
    if (!user_id.empty()) {
      Logger::Instance().Info("User disconnected: " + user_id);
    }
    state->RemoveSession(session.get());
  };

  auto ws_disconnect = [state](const std::shared_ptr<chirp::network::Session>& session) {
    state->RemoveSession(session.get());
  };

  auto server = chirp::chat::runtime::MakeDistributedTcpServer(io, port, on_packet, tcp_disconnect);
  auto ws_server = chirp::chat::runtime::MakeDistributedWsServer(io, ws_port, on_packet, ws_disconnect);

  server->Start();
  ws_server->Start();

  Logger::Instance().Info("Enhanced Chat service started, listening on TCP:" + std::to_string(port) +
                          " WS:" + std::to_string(ws_port));

  chirp::chat::runtime::InstallSignalStop(io, [&]() {
    Logger::Instance().Info("Shutting down chat service...");
    server->Stop();
    ws_server->Stop();
    router->Stop();
    delivery_tracker->Stop();
    migration_worker->Stop();
    io.stop();
  });
  io.run();
  Logger::Instance().Info("chirp_chat_enhanced exited");
  return 0;
}
