// Enhanced Distributed Chat Service with Hybrid Message Store
// Features: Redis+MySQL dual-write, message delivery tracking, pagination

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <functional>
#include <random>

#include <asio.hpp>

#include "hybrid_message_store.h"
#include "chat_rate_limiter.h"
#include "channel_pacer.h"
#include "chat_validation.h"
#include "delivery_ack_manager.h"
#include "repeat_guard.h"
#include "inject_consumer.h"
#include "login_token_verifier.h"
#include "message_delivery_tracker.h"
#include "message_migration_worker.h"
#include "npc_uplink.h"
#include "paginated_history_retriever.h"
#include "player_directory.h"
#include "push_bridge.h"
#include "word_filter.h"

#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/server_gateway_peer.h"
#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "logger.h"
#include "network/message_router.h"
#include "network/notification_client.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "runtime_utils.h"

namespace {

using chirp::chat::HybridMessageStore;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::PaginatedHistoryRetriever;
using chirp::chat::MessageStoreConfig;
using chirp::common::Logger;

/// @brief Distributed chat state management
///
/// Session bindings live in the shared SessionRegistry - the same
/// (user, device) store the basic build uses, so a rebinding login kicks the
/// previous session of the same pair while another device of the user
/// coexists, instead of silently overwriting it and leaving a zombie.
struct DistributedChatState {
  std::shared_ptr<chirp::network::SessionRegistry> registry =
      std::make_shared<chirp::network::SessionRegistry>();

  // Current instance ID
  std::string instance_id;

  // Returns the session previously bound to the same (user, device) pair so
  // the caller can kick it; null when the slot was free, stale, or this very
  // connection. The device id is normalized inside the registry.
  std::shared_ptr<chirp::network::Session> AddSession(
      const std::string& user_id, const std::string& device_id,
      const std::string& session_id,
      const std::shared_ptr<chirp::network::Session>& session) {
    return chirp::network::BindAuthenticatedSession(registry, user_id, session_id,
                                                    device_id, session);
  }

  // Only releases the (user, device) slot while it still points at THIS
  // session: a newer login for the same pair may already own it, and a stale
  // disconnect (e.g. the send client's late FIN) must not unregister the
  // current session.
  void RemoveSession(const std::shared_ptr<chirp::network::Session>& session) {
    chirp::network::RemoveAuthenticatedSession(registry, session);
  }

  std::string GetUserId(const std::shared_ptr<chirp::network::Session>& session) {
    return chirp::network::GetAuthenticatedSession(registry, session).user_id;
  }
};

/// @brief Tell a session it was displaced and close it (same contract as the
/// basic build's KickSession).
void KickSession(const std::shared_ptr<chirp::network::Session>& session,
                 const std::string& reason) {
  chirp::auth::KickNotify kick;
  kick.set_reason(reason.empty() ? "kicked" : reason);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(kick.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

// All live sessions of a user whose connection has not half-closed - the
// healthy delivery targets across every device. Empty means the user has no
// connection worth writing to right now.
std::vector<std::shared_ptr<chirp::network::Session>> HealthyLocalSessions(
    const std::shared_ptr<DistributedChatState>& state, const std::string& user_id) {
  std::vector<std::shared_ptr<chirp::network::Session>> healthy;
  for (const auto& recv : chirp::network::GetUserSessions(state->registry, user_id)) {
    if (!recv->PeerHalfClosed()) {
      healthy.push_back(recv);
    }
  }
  return healthy;
}

// Holds a delivery until MESSAGE_ACK when any target device declared the
// ack capability. Track is idempotent per message id, so one capable device
// is enough; the ack may arrive over a different one. Returns whether the
// delivery is now held for an ack.
bool TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks,
                       const std::vector<std::shared_ptr<chirp::network::Session>>& sessions,
                       const std::string& message_id,
                       const std::string& receiver_id,
                       const std::string& payload) {
  if (!acks) {
    return false;
  }
  for (const auto& recv : sessions) {
    if (acks->IsCapable(recv.get())) {
      acks->Track(message_id, receiver_id, payload);
      return true;
    }
  }
  return false;
}

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
                      chirp::network::ServerGatewayPeer* hub_peer,
                      const std::string& npc_service_id,
                      const std::string& npc_prefix,
                      chirp::network::ChatPeerLink* spoke_link,
                      const std::string& spoke_game_id,
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
        // A receiver whose connections already sent FIN would "consume" the
        // message without ever reading it; report not-delivered so the
        // caller queues it offline.
        auto healthy = HealthyLocalSessions(state, user_id);
        if (healthy.empty()) {
          return false;
        }
        // Ack-capable sessions hold the delivery until MESSAGE_ACK; only
        // legacy sessions keep the write-means-delivered self answer.
        if (!TrackAckIfCapable(acks, healthy, msg.message_id(), user_id, msg_bytes)) {
          delivery_tracker->Acknowledge(msg.message_id(), user_id);
        }
        for (const auto& recv_session : healthy) {
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
        }
        Logger::Instance().Info("Message delivered locally to " + user_id + " (" +
                                std::to_string(healthy.size()) + " session(s))");
        return true;
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
    // Spoke mode: relay non-private channels up to the app-plane hub.
    // Best-effort by design — an unregistered link drops the uplink and
    // the game plane keeps running.
    if (spoke_link != nullptr && spoke_link->registered()) {
      chirp::gateway::ChannelMessageNotify notify;
      notify.set_game_id(spoke_game_id);
      notify.set_channel_id(req.channel_id());
      *notify.mutable_message() = msg;
      spoke_link->SendChannelMessage(notify);
    }
  }
}

/// @brief Internal-plane trust gate (parity with the basic build's main.cc):
/// an edge gateway dials in with SERVER_AUTH_REQ carrying the shared service
/// secret, and its per-client pipes then skip the per-IP login limiter (a
/// gateway fans many users out of one source address). Token verification on
/// the LOGIN_REQ that follows is untouched. With no secret configured the
/// frame is ignored - direct entry only, the historical enhanced behavior.
/// Returns true when the frame was consumed (response sent).
bool HandleServerAuth(const chirp::gateway::Packet& pkt,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const std::string& secret,
                      std::unordered_set<const chirp::network::Session*>* trusted) {
  if (secret.empty()) {
    return false;
  }
  chirp::game_server_gateway::ServerAuthRequest auth_req;
  chirp::game_server_gateway::ServerAuthResponse auth_resp;
  auth_resp.set_server_time_ms(chirp::chat::runtime::NowMs());
  if (!auth_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size())) ||
      auth_req.secret() != secret) {
    auth_resp.set_code(chirp::common::AUTH_FAILED);
    // Reply-then-close: the gateway pipe must not linger after a rejected
    // secret (the basic build's SendPacketAndClose semantics).
    const auto framed = chirp::network::ProtobufFraming::Encode(
        [&] {
          chirp::gateway::Packet p;
          p.set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
          p.set_sequence(pkt.sequence());
          p.set_body(auth_resp.SerializeAsString());
          return p;
        }());
    session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
    return true;
  }
  if (trusted != nullptr) {
    trusted->insert(session.get());
  }
  auth_resp.set_code(chirp::common::OK);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(),
                                   auth_resp.SerializeAsString());
  return true;
}

/// @brief Handle user login
void HandleLogin(const chirp::auth::LoginRequest& req,
                const std::shared_ptr<chirp::network::Session>& session,
                const std::shared_ptr<DistributedChatState>& state,
                const std::shared_ptr<HybridMessageStore>& store,
                const std::shared_ptr<chirp::network::MessageRouter>& router,
                const chirp::common::LoginTokenVerifier* token_verifier,
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
    resp.set_kick_previous(true);
    resp.mutable_kick()->set_reason("login from another device");

    // Rebinding the same (user, device) pair displaces the previous session,
    // which gets a KICK_NOTIFY instead of silently rotting; another device
    // of the same user keeps its session.
    auto old = state->AddSession(user_id, req.device_id(), resp.session_id(), session);
    if (old && old.get() != session.get()) {
      KickSession(old, "login from another device");
    }

    if (acks && req.supports_message_ack()) {
      acks->MarkCapable(session);
    }

    // Cross-instance deliveries fan to every live local session of the user
    // (one per device) through the registry - the callback outlives any
    // single connection, so it must not capture one.
    router->SubscribeUserChat(user_id, [state, acks, user_id](const std::string& msg_data) {
      chirp::chat::ChatMessage msg;
      if (!msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        return;
      }
      auto healthy = HealthyLocalSessions(state, user_id);
      if (healthy.empty()) {
        return;
      }
      // Cross-instance deliveries are tracked like local ones - this
      // instance owns the receiving sessions, so the ack comes back here.
      TrackAckIfCapable(acks, healthy, msg.message_id(), user_id, msg_data);
      for (const auto& recv : healthy) {
        chirp::chat::runtime::SendChatNotify(recv, msg);
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

  // Lexicon content filter (game_chat_features P0 敏感词过滤). Empty path
  // keeps it off entirely.
  const std::string word_filter_file =
      chirp::chat::runtime::GetArg(argc, argv, "--word_filter_file", "");
  const std::string word_filter_policy =
      chirp::chat::runtime::GetArg(argc, argv, "--word_filter_policy", "replace");

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
  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::app_notification::NotificationClient>(
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
  std::shared_ptr<chirp::network::ServerGatewayPeer> hub_peer;
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
      auto healthy = HealthyLocalSessions(state, receiver_id);
      if (healthy.empty()) {
        Logger::Instance().Info("inject receiver not online: " + receiver_id);
        return false;
      }
      // Injected private replies are tracked like SEND_MESSAGE deliveries;
      // only legacy sessions keep the write-means-delivered self answer.
      if (!TrackAckIfCapable(acks.get(), healthy, msg.message_id(), receiver_id,
                             msg.SerializeAsString())) {
        delivery_tracker->Acknowledge(msg.message_id(), receiver_id);
      }
      for (const auto& recv_session : healthy) {
        chirp::chat::runtime::SendChatNotify(recv_session, msg);
      }
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

    chirp::network::ServerGatewayPeer::Options hub_options;
    hub_options.host = hub_host;
    hub_options.port = hub_port;
    hub_options.service_id =
        chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_service", "chat");
    hub_options.secret = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_secret", "");
    hub_options.reconnect_delay_seconds = chirp::chat::runtime::ParseIntArg(
        argc, argv, "--server_gateway_reconnect", 3);

    const std::string hub_service = hub_options.service_id;
    auto consumer = std::make_shared<chirp::chat::InjectConsumer>(std::move(hooks));
    hub_peer = chirp::network::ServerGatewayPeer::Create(
        io, std::move(hub_options),
        [consumer](const chirp::game_server_gateway::InjectMessageNotify& notify) {
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
  chirp::common::LoginTokenVerifier token_verifier(token_secret);

  // App-plane player directory (WP-8): the identity/subscription/unread
  // registries relocated from game_server_gateway, plus the hub fan-out
  // tail. Optional per-registry Redis write-through keeps records across
  // app_chat restarts; empty hosts stay memory-only.
  const std::string binding_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--binding_redis_host", "");
  const uint16_t binding_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--binding_redis_port", 6379);
  const std::string subscription_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--subscription_redis_host", "");
  const uint16_t subscription_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--subscription_redis_port", 6379);
  const std::string unread_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--unread_redis_host", "");
  const uint16_t unread_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--unread_redis_port", 6379);
  chirp::chat::PlayerDirectory::Options directory_options;
  directory_options.max_fanout_per_message = static_cast<size_t>(chirp::chat::runtime::ParseIntArg(
      argc, argv, "--max_fanout_per_message", 10000));
  if (!binding_redis_host.empty()) {
    directory_options.identities_redis = [host = binding_redis_host, port = binding_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  if (!subscription_redis_host.empty()) {
    directory_options.subscriptions_redis = [host = subscription_redis_host,
                                             port = subscription_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  if (!unread_redis_host.empty()) {
    directory_options.unread_redis = [host = unread_redis_host, port = unread_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  // One private copy per subscriber through the same store/deliver tail as
  // an injected private message: hybrid storage, healthy-session push with
  // ack tracking, offline queue + push when nobody is online.
  directory_options.deliver_copy =
      [&state, &store, &delivery_tracker, acks, &push](
          const std::string& player_id, const chirp::gateway::ChannelMessageNotify& notify) {
        chirp::chat::ChatMessage copy = notify.message();
        copy.set_message_id(chirp::chat::runtime::GenerateMessageId());
        // The App client answers to the namespaced game identity — the
        // same "{game_id}:" prefix the cross-plane reply path parses.
        copy.set_sender_id(notify.game_id() + ":" + notify.message().sender_id());
        copy.set_receiver_id(player_id);
        copy.set_channel_type(chirp::chat::PRIVATE);
        copy.set_channel_id(HybridMessageStore::PrivateChannelId(copy.sender_id(), player_id));
        copy.set_sender_kind(chirp::chat::SENDER_USER);
        chirp::chat::MessageData data = ToMessageData(copy);
        store->StoreMessageAsync(data, [](bool ok) {
          if (!ok) {
            Logger::Instance().Warn("fan-out copy storage to MySQL failed");
          }
        });
        auto healthy = HealthyLocalSessions(state, player_id);
        if (!healthy.empty()) {
          if (!TrackAckIfCapable(acks.get(), healthy, copy.message_id(), player_id,
                                 copy.SerializeAsString())) {
            delivery_tracker->Acknowledge(copy.message_id(), player_id);
          }
          for (const auto& recv_session : healthy) {
            chirp::chat::runtime::SendChatNotify(recv_session, copy);
          }
        } else {
          store->AddOfflineMessage(player_id, data.SerializeAsString());
          push.NotifyOffline(copy, player_id);
        }
      };
  chirp::chat::PlayerDirectory directory(std::move(directory_options));
  directory.LoadAll();

  // Hub mode: accept game_chat peer registrations on the dedicated peer port
  // (app_chat deployment). The main client port keeps serving the client
  // protocol only.
  const bool hub_mode = chirp::chat::runtime::ParseIntArg(argc, argv, "--hub_mode", 0) != 0;
  std::shared_ptr<chirp::network::ChatPeerHub> chat_hub;
  if (hub_mode) {
    chirp::network::ChatPeerHub::Options hub_opts;
    hub_opts.port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--hub_peer_port", 8200);
    hub_opts.min_peer_version = chirp::chat::runtime::ParseIntArg(argc, argv, "--min_peer_version", 1);
    hub_opts.allow_unknown_peers =
        chirp::chat::runtime::ParseIntArg(argc, argv, "--allow_unknown_peers", 0) != 0;
    // Parse allowed_peers: "id1:secret1,id2:secret2"
    const std::string allowed_peers_str =
        chirp::chat::runtime::GetArg(argc, argv, "--allowed_peers", "");
    if (!allowed_peers_str.empty()) {
      std::string s = allowed_peers_str;
      while (!s.empty()) {
        auto comma = s.find(',');
        std::string pair = s.substr(0, comma);
        auto colon = pair.find(':');
        if (colon != std::string::npos) {
          hub_opts.allowed_peers[pair.substr(0, colon)] = pair.substr(colon + 1);
        }
        if (comma == std::string::npos) break;
        s = s.substr(comma + 1);
      }
    }
    chat_hub = chirp::network::ChatPeerHub::Create(
        io, std::move(hub_opts),
        [](const std::string& service_id, const std::string& game_id, int32_t version,
           const std::vector<chirp::gateway::PeerCapability>&) {
          Logger::Instance().Info("peer registered: service_id=" + service_id +
                                  " game_id=" + game_id + " version=" + std::to_string(version));
        },
        [](const std::string& service_id, const std::string& reason) {
          Logger::Instance().Info("peer dropped: service_id=" + service_id + " reason=" + reason);
        },
        [&directory](const std::string& service_id,
                     const chirp::gateway::ChannelMessageNotify& notify) {
          // Cross-plane fan-out (TODO 55): resolve the channel's
          // subscribers in the player directory and hand each one a
          // private copy (plus one unread badge increment per copy).
          const size_t copies = directory.FanoutChannelMessage(notify);
          Logger::Instance().Info("channel message uplink: service_id=" + service_id +
                                  " game_id=" + notify.game_id() +
                                  " channel=" + notify.channel_id() +
                                  " copies=" + std::to_string(copies));
        });
    chat_hub->Start();
    Logger::Instance().Info("hub mode enabled: peer_port=" + std::to_string(chat_hub->port()));
  }

  // Spoke mode: register into an app_chat hub (game_chat deployment). Needs
  // host + service_id + game_id all set; anything missing keeps the spoke
  // fully off. Uplink relays non-private channels best-effort — an
  // unregistered link drops the uplink and the game plane keeps running.
  std::shared_ptr<chirp::network::ChatPeerLink> spoke_link;
  std::string spoke_game_id;
  const std::string app_chat_host = chirp::chat::runtime::GetArg(argc, argv, "--app_chat_host", "");
  if (!app_chat_host.empty()) {
    const std::string game_service_id =
        chirp::chat::runtime::GetArg(argc, argv, "--game_service_id", "");
    const std::string game_id_arg = chirp::chat::runtime::GetArg(argc, argv, "--game_id", "");
    if (game_service_id.empty() || game_id_arg.empty()) {
      Logger::Instance().Warn(
          "--app_chat_host is set but --game_service_id/--game_id is not; the "
          "peer spoke stays disabled");
    } else {
      chirp::network::ChatPeerLink::Options link_opts;
      link_opts.host = app_chat_host;
      link_opts.port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--app_chat_port", 8200);
      link_opts.service_id = game_service_id;
      link_opts.secret = chirp::chat::runtime::GetArg(argc, argv, "--game_service_secret", "");
      link_opts.game_id = game_id_arg;
      link_opts.supported_features = {chirp::gateway::RELAY_READ_RECEIPTS,
                                      chirp::gateway::RELAY_TYPING,
                                      chirp::gateway::RELAY_PRESENCE};
      spoke_link = chirp::network::ChatPeerLink::Create(
          io, std::move(link_opts),
          [game_id_arg](int32_t version,
                        const std::vector<chirp::gateway::PeerCapability>&) {
            Logger::Instance().Info("spoke registered to hub: game_id=" + game_id_arg +
                                    " negotiated_version=" + std::to_string(version));
          },
          // A hub never sends CHANNEL_MESSAGE_NOTIFY downstream; leave the
          // handler unset (an unexpected frame is logged by the link).
          nullptr,
          // Hub -> spoke player reply: the hub already resolved
          // player_id -> game_user_id. Delivered through the same
          // store + local-session tail as an injected private message.
          [&state, &store, &delivery_tracker, acks](
              const chirp::gateway::PeerInjectMessageNotify& inject) {
            chirp::chat::ChatMessage msg;
            msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
            msg.set_sender_id(inject.sender_id());
            msg.set_receiver_id(inject.channel_id());  // channel_id carries the target user
            msg.set_channel_type(chirp::chat::PRIVATE);
            msg.set_channel_id(inject.channel_id());
            msg.set_msg_type(chirp::chat::TEXT);
            msg.set_content(inject.content());
            msg.set_timestamp(chirp::chat::runtime::NowMs());
            msg.set_sender_kind(chirp::chat::SENDER_USER);
            chirp::chat::MessageData data = ToMessageData(msg);
            store->StoreMessageAsync(data, [](bool ok) {
              if (!ok) {
                Logger::Instance().Warn("peer inject storage to MySQL failed");
              }
            });
            auto healthy = HealthyLocalSessions(state, inject.channel_id());
            if (!healthy.empty()) {
              if (!TrackAckIfCapable(acks.get(), healthy, msg.message_id(), inject.channel_id(),
                                     msg.SerializeAsString())) {
                delivery_tracker->Acknowledge(msg.message_id(), inject.channel_id());
              }
              for (const auto& recv_session : healthy) {
                chirp::chat::runtime::SendChatNotify(recv_session, msg);
              }
              Logger::Instance().Info("peer inject delivered live to " + inject.channel_id());
            } else {
              store->AddOfflineMessage(inject.channel_id(), data.SerializeAsString());
              Logger::Instance().Info("peer inject queued offline for " + inject.channel_id());
            }
          },
          []() {
            Logger::Instance().Warn("spoke lost the hub connection, will retry");
          });
      spoke_game_id = game_id_arg;
      spoke_link->Start();
      Logger::Instance().Info("spoke mode enabled: game_id=" + spoke_game_id +
                              " -> hub=" + app_chat_host);
    }
  }

  // Internal-plane trust gate + per-IP login limiter (parity with the basic
  // build's main.cc). The limiter defaults OFF here so the historical
  // enhanced behavior is unchanged unless explicitly enabled; the trust gate
  // matters even without a limiter once a gateway fronts the direct entry.
  const std::string gateway_service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--gateway_service_secret", "");
  const int login_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--login_rate_limit_per_min", 0);
  // Basic-build parity: the per-user send window exists here too, but keeps
  // the enhanced form's default-off stance — set --send_rate_limit_per_min
  // to enable it explicitly.
  const int send_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--send_rate_limit_per_min", 0);

  chirp::chat::ChatRateLimiter::Config edge_rate_config;
  edge_rate_config.max_logins_per_minute_per_ip = login_rate_limit_per_min;
  edge_rate_config.max_sends_per_minute_per_user = send_rate_limit_per_min;
  std::shared_ptr<chirp::network::RedisClient> edge_limiter_redis;
  if (login_rate_limit_per_min > 0 || send_rate_limit_per_min > 0) {
    edge_limiter_redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }
  chirp::chat::ChatRateLimiter edge_rate_limiter(edge_limiter_redis, edge_rate_config);
  auto trusted_conns =
      std::make_shared<std::unordered_set<const chirp::network::Session*>>();

  chirp::chat::WordFilterOptions word_filter_options;
  word_filter_options.lexicon_path = word_filter_file;
  word_filter_options.policy = chirp::chat::WordFilterPolicyFromString(word_filter_policy);
  chirp::chat::WordFilter word_filter(word_filter_options);
  chirp::chat::ChannelPacer channel_pacer;
  chirp::chat::RepeatGuard repeat_guard;

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router, &token_verifier, acks](
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::auth::LoginRequest& req,
                          int64_t seq) {
    HandleLogin(req, session, state, store, router, &token_verifier, acks.get(), seq);
  };
  handlers.on_send_message = [state, store, delivery_tracker, acks, router,
                              peer = hub_peer.get(), npc_service_id, npc_prefix,
                              link = spoke_link.get(), spoke_game_id,
                              hub = chat_hub.get(), &directory, &word_filter,
                              &channel_pacer, &repeat_guard,
                              send_gate = &edge_rate_limiter](
                                 const std::shared_ptr<chirp::network::Session>& session,
                                 const chirp::chat::SendMessageRequest& req,
                                 int64_t seq) {
    // Per-channel content cap (game_chat_features P0 长度限制) — checked
    // before the filter so over-long messages are refused without spending
    // lexicon work. Same shared validation the basic form gets for free.
    if (chirp::chat::ValidateContentLength(req) != chirp::common::OK) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                       resp.SerializeAsString());
      return;
    }
    const std::string authenticated_user = state->GetUserId(session);
    // Direct-entry abuse gate (basic-build parity): the per-user send window
    // counts only authenticated sends. Inert without --send_rate_limit_per_min
    // (or without Redis), mirroring the basic build's fail-open contract.
    if (!authenticated_user.empty() && send_gate != nullptr) {
      const auto gate = send_gate->CheckSend(authenticated_user);
      if (!gate.allowed) {
        chirp::chat::SendMessageResponse resp;
        resp.set_code(chirp::common::RATE_LIMITED);
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                         resp.SerializeAsString());
        return;
      }
    }
    // Per-channel pacing (game_chat_features P0 发送频率限制): world 5s,
    // guild 2s, private 1s between sends by the same user, keyed by the
    // authenticated identity and anchored to the last allowed send.
    if (!authenticated_user.empty() &&
        !channel_pacer.Allow(authenticated_user, req.channel_type(), chirp::chat::runtime::NowMs())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::RATE_LIMITED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                       resp.SerializeAsString());
      return;
    }
    // Repeat-message mute (game_chat_features P0 重复消息检测): the third
    // consecutive identical send starts a 5-minute mute during which every
    // send is refused. Same position as the basic build: after pacing,
    // before the filter.
    if (!authenticated_user.empty() &&
        !repeat_guard.Allow(authenticated_user, req.content(), chirp::chat::runtime::NowMs())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::RATE_LIMITED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                       resp.SerializeAsString());
      return;
    }
    // Lexicon content filter before every delivery path — the cross-plane
    // intercept below included, so filtered content never reaches the game
    // plane either. kReplace filters the content in place; kReject refuses
    // with INVALID_PARAM (no dedicated result code exists yet).
    chirp::chat::SendMessageRequest working = req;
    if (word_filter.enabled()) {
      std::string filtered = working.content();
      if (!word_filter.Filter(working.sender_id(), &filtered)) {
        chirp::chat::SendMessageResponse resp;
        resp.set_code(chirp::common::INVALID_PARAM);
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                         resp.SerializeAsString());
        return;
      }
      working.set_content(filtered);
    }
    // Cross-plane reply (TODO 56): a channel_id prefixed
    // "<game_id>:<bare>" is a reply into the game plane — resolve the
    // sender's game_user_id and inject it into the game_chat spoke that
    // registered the game. Only hub mode has spokes; anything else falls
    // through to the ordinary send path below.
    if (hub) {
      const auto outcome = directory.RelayGameReply(
          working.sender_id(), working.channel_id(), working.content(),
          /*client_msg_id=*/"",
          [hub](const std::string& game_id) { return hub->service_id_for_game(game_id); },
          [hub](const std::string& service_id,
                const chirp::gateway::PeerInjectMessageNotify& notify) {
            return hub->SendInject(service_id, notify);
          });
      if (outcome != chirp::chat::PlayerDirectory::GameReplyOutcome::kNoGamePrefix) {
        chirp::chat::SendMessageResponse resp;
        switch (outcome) {
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kSent:
            // The game plane mints its own message id; the App client gets
            // the acceptance only.
            resp.set_code(chirp::common::OK);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kUnboundPlayer:
            resp.set_code(chirp::common::INVALID_PARAM);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kUnknownGame:
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kSendFailed:
            resp.set_code(chirp::common::SERVER_UNAVAILABLE);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kNoGamePrefix:
            break;  // handled above; unreachable
        }
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                         resp.SerializeAsString());
        return;
      }
    }
    HandleSendMessage(working, session, state, store, delivery_tracker, acks.get(), router,
                      peer, npc_service_id, npc_prefix, link, spoke_game_id, seq);
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
    state->RemoveSession(session);
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };
  handlers.on_message_ack = [state, acks, &delivery_tracker](
                                const std::shared_ptr<chirp::network::Session>& session,
                                const chirp::chat::MessageAck& req,
                                int64_t /*seq*/) {
    const std::string user_id = state->GetUserId(session);
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

  auto on_packet = [handlers, gateway_service_secret, trusted_conns, &edge_rate_limiter,
                    &directory](const std::shared_ptr<chirp::network::Session>& session,
                                const chirp::gateway::Packet& pkt) {
    if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
      HandleServerAuth(pkt, session, gateway_service_secret, trusted_conns.get());
      return;
    }
    // WP-8 player-directory block (5013-5030) rides this port behind the
    // same trust gate: game backends assert bindings/subscriptions here and
    // the app edge self-serves through it.
    if (chirp::chat::DispatchPlayerDirectoryPacket(pkt, session, directory,
                                                   trusted_conns.get())) {
      return;
    }
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ &&
        trusted_conns->count(session.get()) == 0) {
      // Direct-entry abuse gate, mirroring the basic build: trusted gateway
      // pipes are exempt, every direct LOGIN_REQ consumes budget first.
      const auto gate = edge_rate_limiter.CheckLogin(session->RemoteAddress());
      if (!gate.allowed) {
        chirp::auth::LoginResponse deny;
        deny.set_code(chirp::common::RATE_LIMITED);
        deny.set_server_time(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(),
                                         deny.SerializeAsString());
        return;
      }
    }
    chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  };

  auto tcp_disconnect = [state, acks, trusted_conns](const std::shared_ptr<chirp::network::Session>& session) {
    std::string user_id = state->GetUserId(session);
    if (!user_id.empty()) {
      Logger::Instance().Info("User disconnected: " + user_id);
    }
    acks->ForgetSession(session.get());
    // Drop any trust-grant bound to this connection so a reused pointer
    // cannot inherit the previous connection's limiter bypass.
    trusted_conns->erase(session.get());
    state->RemoveSession(session);
  };

  auto ws_disconnect = [state, acks, trusted_conns](const std::shared_ptr<chirp::network::Session>& session) {
    acks->ForgetSession(session.get());
    trusted_conns->erase(session.get());
    state->RemoveSession(session);
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
    if (spoke_link) {
      spoke_link->Stop();
    }
    if (chat_hub) {
      chat_hub->Stop();
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
