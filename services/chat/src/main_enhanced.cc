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
#include "delivery_ack_manager.h"
#include "inject_consumer.h"
#include "login_token_verifier.h"
#include "message_delivery_tracker.h"
#include "message_migration_worker.h"
#include "npc_uplink.h"
#include "paginated_history_retriever.h"
#include "push_bridge.h"
#include "server_gateway_peer.h"
#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "logger.h"
#include "network/message_router.h"
#include "network/notification_client.h"
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
      // Only clear the user slot while it still points at THIS session: a
      // newer login for the same user may already own it, and a stale
      // disconnect (e.g. the send client's late FIN) must not unregister
      // the current session.
      auto sit = local_sessions.find(it->second);
      if (sit != local_sessions.end() && sit->second.lock().get() == session) {
        local_sessions.erase(sit);
      }
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

/// @brief Convert a protocol ChatMessage into the store's MessageData.
chirp::chat::MessageData ToMessageData(const chirp::chat::ChatMessage& msg) {
  chirp::chat::MessageData data;
  data.message_id = msg.message_id();
  data.sender_id = msg.sender_id();
  data.receiver_id = msg.receiver_id();
  data.channel_id = msg.channel_id();
  data.channel_type = msg.channel_type();
  data.msg_type = msg.msg_type();
  data.content = msg.content();
  data.timestamp = msg.timestamp();
  data.created_at = chirp::chat::runtime::NowMs();
  return data;
}

/// @brief Handle send message with hybrid storage
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                      const std::shared_ptr<chirp::network::Session>& sender_session,
                      const std::shared_ptr<DistributedChatState>& state,
                      const std::shared_ptr<HybridMessageStore>& store,
                      const std::shared_ptr<MessageDeliveryTracker>& delivery_tracker,
                      chirp::chat::DeliveryAckManager* acks,
                      const std::shared_ptr<chirp::network::MessageRouter>& router,
                      chirp::chat::ServerGatewayPeer* hub_peer,
                      const std::string& npc_service_id,
                      const std::string& npc_prefix,
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
  chirp::chat::MessageData msg_data = ToMessageData(msg);

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
    // NPC-addressed private messages bypass player delivery: the receiver is
    // not a user, so there is nothing to route and nothing to queue offline.
    // The utterance is published to the NPC dialog service (fire-and-forget;
    // OK means accepted, not that a reply will come) and the NPC answers
    // through the injection path.
    if (hub_peer != nullptr && !npc_service_id.empty() &&
        chirp::chat::npc::IsNpcReceiver(npc_prefix, req.receiver_id())) {
      hub_peer->SendEventPublish(
          chirp::chat::npc::MakeUtteranceEvent(msg, npc_prefix, npc_service_id),
          [message_id = msg.message_id()](chirp::common::ErrorCode code) {
            if (code != chirp::common::OK) {
              Logger::Instance().Warn(
                  "npc utterance publish failed for message " + message_id +
                  " (code " + std::to_string(static_cast<int>(code)) + ")");
            } else {
              Logger::Instance().Info("npc utterance published for message " + message_id);
            }
          });
      return;
    }

    // One serialization feeds both the router fan-out and the ack pending
    // entry, so a late-ack offline removal matches byte-for-byte.
    const std::string msg_bytes = msg.SerializeAsString();
    const int64_t receivers = router->SendChatMessageCount(req.receiver_id(), msg_bytes,
      [&](const std::string& user_id) -> bool {
        auto recv_session = state->GetLocalSession(user_id);
        // A receiver whose connection already sent FIN would "consume" the
        // message without ever reading it; report not-delivered so the
        // caller queues it offline.
        if (recv_session && !recv_session->PeerHalfClosed()) {
          // Ack-capable sessions hold the delivery until MESSAGE_ACK; only
          // legacy sessions keep the write-means-delivered self answer.
          const bool capable = acks && acks->IsCapable(recv_session.get());
          if (capable) {
            acks->Track(msg.message_id(), user_id, msg_bytes);
          } else {
            delivery_tracker->Acknowledge(msg.message_id(), user_id);
          }
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        }
        return false;
      });

    // Nobody received it live (offline here, or on another instance with no
    // Redis pub/sub): enqueue an offline copy. AddOfflineMessage falls back
    // to an in-memory queue when Redis is unavailable, so single-node
    // deployments without Redis still refill on login.
    if (receivers <= 0) {
      store->AddOfflineMessage(req.receiver_id(), msg_data.SerializeAsString());
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
                const chirp::chat::LoginTokenVerifier* token_verifier,
                chirp::chat::DeliveryAckManager* acks,
                int64_t seq) {
  std::string user_id;

  // With a shared secret configured, the token must be an HS256 JWT with
  // an exp claim and the user in sub (same contract as the basic build).
  if (token_verifier != nullptr && token_verifier->enabled()) {
    std::string verify_err;
    if (!token_verifier->Verify(req.token(), chirp::chat::runtime::NowMs(),
                                &user_id, &verify_err)) {
      Logger::Instance().Warn("chat login rejected: " + verify_err);
      chirp::auth::LoginResponse deny;
      deny.set_code(chirp::common::AUTH_FAILED);
      deny.set_server_time(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq,
                                       deny.SerializeAsString());
      return;
    }
  } else {
    // Scaffolding login: treat token as user_id.
    user_id = req.token();
  }

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(chirp::chat::runtime::NowMs()));

    state->AddSession(user_id, session);

    if (acks && req.supports_message_ack()) {
      acks->MarkCapable(session);
    }

    // Subscribe to user's chat channel
    std::string channel = chirp::network::RouterChannels::UserChat(user_id);
    router->SubscribeUserChat(user_id, [session, state, acks, user_id](const std::string& msg_data) {
      auto s = session;
      if (s) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
          // Cross-instance deliveries are tracked like local ones - this
          // instance owns the receiving session, so the ack comes back here.
          if (acks && acks->IsCapable(session.get())) {
            acks->Track(msg.message_id(), user_id, msg_data);
          }
          chirp::chat::runtime::SendChatNotify(s, msg);
        }
      }
    });

    Logger::Instance().Info("User logged in: " + user_id);
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());

  chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());

  // Deliver offline messages only after the LOGIN_RESP (same order as the
  // basic build): a refill notify sent first would be swallowed by clients
  // that read exactly one frame as "the login response".
  if (!user_id.empty()) {
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
      // Refills are tracked like live deliveries: an unacked refill returns
      // to the offline queue instead of dying with the connection.
      if (acks && acks->IsCapable(session.get())) {
        acks->Track(msg.message_id(), user_id, msg.SerializeAsString());
      }
      chirp::chat::runtime::SendChatNotify(session, msg);
    }
  }
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

  // 0 disables client delivery-ack tracking entirely (kill switch).
  const int64_t ack_timeout_ms = chirp::chat::runtime::ParseIntArg(argc, argv, "--ack_timeout_ms", 10000);

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

  // Offline pushes are wired only when a notification service is configured;
  // a null client makes the bridge a no-op.
  const std::string notification_host = chirp::chat::runtime::GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--notification_port", 5006);
  std::shared_ptr<chirp::notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::notification::NotificationClient>(
        io, notification_host, notification_port);
  }
  chirp::chat::PushBridge push(notification);

  // Client delivery-ack bookkeeping: live deliveries to ack-capable sessions
  // stay pending until MESSAGE_ACK; the timeout hands them back to the
  // offline queue (no push notification - the receiver was just online).
  chirp::chat::DeliveryAckManager::Config ack_config;
  ack_config.timeout_ms = ack_timeout_ms;
  auto acks = std::make_shared<chirp::chat::DeliveryAckManager>(
      io, ack_config,
      [store](const std::string& receiver_id, const std::string& payload) {
        store->AddOfflineMessage(receiver_id, payload);
      },
      [store](const std::string& receiver_id, const std::string& payload) {
        store->RemoveOfflineMessage(receiver_id, payload);
      });

  // Server-plane injection: when --server_gateway_host is set, chat dials the
  // hub as an internal service and delivers forwarded injections through the
  // same store/deliver tail as SEND_MESSAGE. Injection senders are non-user
  // identities, so they skip the router's cross-instance fan-out and go
  // straight to the local session/offline queue.
  const std::string hub_host = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_host", "");
  const std::string npc_service_id = chirp::chat::runtime::GetArg(argc, argv, "--npc_service_id", "");
  const std::string npc_prefix = chirp::chat::runtime::GetArg(argc, argv, "--npc_prefix", "npc:");
  if (!npc_service_id.empty() && hub_host.empty()) {
    Logger::Instance().Warn(
        "--npc_service_id is set but --server_gateway_host is not; the NPC "
        "uplink stays disabled");
  }
  std::shared_ptr<chirp::chat::ServerGatewayPeer> hub_peer;
  if (!hub_host.empty()) {
    const uint16_t hub_port = chirp::chat::runtime::ParseU16Arg(
        argc, argv, "--server_gateway_port", 8100);
    chirp::chat::InjectHooks hooks;
    hooks.private_channel_id =
        [](const std::string& a, const std::string& b) {
          return HybridMessageStore::PrivateChannelId(a, b);
        };
    hooks.store_message =
        [&store](const chirp::chat::ChatMessage& msg) {
          chirp::chat::MessageData data = ToMessageData(msg);
          store->StoreMessageAsync(data, [](bool ok) {
            if (!ok) {
              Logger::Instance().Warn("injected message storage to MySQL failed");
            }
          });
        };
    hooks.deliver_private =
        [state, &delivery_tracker, acks](const std::string& receiver_id,
                                   const chirp::chat::ChatMessage& msg) -> bool {
      auto recv_session = state->GetLocalSession(receiver_id);
      if (!recv_session || recv_session->PeerHalfClosed()) {
        Logger::Instance().Info("inject receiver not online: " + receiver_id);
        return false;
      }
      // Injected private replies are tracked like SEND_MESSAGE deliveries;
      // only legacy sessions keep the write-means-delivered self answer.
      if (acks && acks->IsCapable(recv_session.get())) {
        acks->Track(msg.message_id(), receiver_id, msg.SerializeAsString());
      } else {
        delivery_tracker->Acknowledge(msg.message_id(), receiver_id);
      }
      chirp::chat::runtime::SendChatNotify(recv_session, msg);
      Logger::Instance().Info("inject delivered live to " + receiver_id);
      return true;
    };
    hooks.queue_offline =
        [&store, &push](const std::string& user_id, const chirp::chat::ChatMessage& msg) {
          chirp::chat::MessageData data = ToMessageData(msg);
          store->AddOfflineMessage(user_id, data.SerializeAsString());
          push.NotifyOffline(msg, user_id);
          Logger::Instance().Info("inject queued offline for " + user_id);
        };
    hooks.broadcast_channel =
        [&router](const std::string& channel_id,
                  const chirp::chat::ChatMessage& msg) -> std::vector<std::string> {
      // Multi-instance fan-out goes through the router; members on other
      // instances are their instances' responsibility, so no local offline
      // members are reported back.
      router->BroadcastToGroup(channel_id, msg.SerializeAsString());
      return {};
    };

    chirp::chat::ServerGatewayPeer::Options hub_options;
    hub_options.host = hub_host;
    hub_options.port = hub_port;
    hub_options.service_id =
        chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_service", "chat");
    hub_options.secret = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_secret", "");
    hub_options.reconnect_delay_seconds = chirp::chat::runtime::ParseIntArg(
        argc, argv, "--server_gateway_reconnect", 3);

    const std::string hub_service = hub_options.service_id;
    auto consumer = std::make_shared<chirp::chat::InjectConsumer>(std::move(hooks));
    hub_peer = chirp::chat::ServerGatewayPeer::Create(
        io, std::move(hub_options),
        [consumer](const chirp::server_gateway::InjectMessageNotify& notify) {
          consumer->HandleInject(notify);
        });
    hub_peer->Start();
    Logger::Instance().Info("server-plane peer enabled hub=" + hub_host + ":" +
                            std::to_string(hub_port) + " service=" + hub_service +
                            (npc_service_id.empty()
                                 ? std::string()
                                 : " npc_service=" + npc_service_id));
  }

  // With a shared secret, LOGIN tokens are verified locally as HS256 JWTs;
  // empty keeps the scaffolding login (token = user id).
  const std::string token_secret = chirp::chat::runtime::GetArg(argc, argv, "--token_secret", "");
  chirp::chat::LoginTokenVerifier token_verifier(token_secret);

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router, &token_verifier, acks](
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::auth::LoginRequest& req,
                          int64_t seq) {
    HandleLogin(req, session, state, store, router, &token_verifier, acks.get(), seq);
  };
  handlers.on_send_message = [state, store, delivery_tracker, acks, router,
                              peer = hub_peer.get(), npc_service_id, npc_prefix](
                                 const std::shared_ptr<chirp::network::Session>& session,
                                 const chirp::chat::SendMessageRequest& req,
                                 int64_t seq) {
    HandleSendMessage(req, session, state, store, delivery_tracker, acks.get(), router,
                      peer, npc_service_id, npc_prefix, seq);
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
  handlers.on_logout = [state, acks](const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::auth::LogoutRequest&,
                               int64_t seq) {
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };
  handlers.on_message_ack = [state, acks, &delivery_tracker](
                                const std::shared_ptr<chirp::network::Session>& session,
                                const chirp::chat::MessageAck& req,
                                int64_t /*seq*/) {
    const std::string user_id = state->GetUserId(session.get());
    if (req.message_id().empty() || user_id.empty() ||
        (!req.user_id().empty() && req.user_id() != user_id)) {
      return;
    }
    if (acks->Acknowledge(req.message_id())) {
      // The real client receipt also settles the delivery tracker's status.
      delivery_tracker->Acknowledge(req.message_id(), user_id);
      Logger::Instance().Info("message acked id=" + req.message_id() + " user=" + user_id);
    }
  };

  auto on_packet = [handlers](const std::shared_ptr<chirp::network::Session>& session,
                              const chirp::gateway::Packet& pkt) {
    chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  };

  auto tcp_disconnect = [state, acks](const std::shared_ptr<chirp::network::Session>& session) {
    std::string user_id = state->GetUserId(session.get());
    if (!user_id.empty()) {
      Logger::Instance().Info("User disconnected: " + user_id);
    }
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
  };

  auto ws_disconnect = [state, acks](const std::shared_ptr<chirp::network::Session>& session) {
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
  };

  auto server = chirp::chat::runtime::MakeDistributedTcpServer(io, port, on_packet, tcp_disconnect);
  auto ws_server = chirp::chat::runtime::MakeDistributedWsServer(io, ws_port, on_packet, ws_disconnect);

  server->Start();
  ws_server->Start();
  acks->Start();

  Logger::Instance().Info("Enhanced Chat service started, listening on TCP:" + std::to_string(port) +
                          " WS:" + std::to_string(ws_port));

  chirp::chat::runtime::InstallSignalStop(io, [&]() {
    Logger::Instance().Info("Shutting down chat service...");
    acks->Stop();
    if (hub_peer) {
      hub_peer->Stop();
    }
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
