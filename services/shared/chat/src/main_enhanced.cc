// Enhanced Distributed Chat Service with Hybrid Message Store
// Features: Redis+MySQL dual-write, message delivery tracking, pagination

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
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
#include "history_compression.h"
#include "delivery_prefs.h"
#include "friend_graph.h"
#include "repeat_guard.h"
#include "inject_consumer.h"
#include "inject_idempotency.h"
#include "login_token_verifier.h"
#include "message_delivery_tracker.h"
#include "group_expiry_sweeper.h"
#include "group_handlers.h"
#include "history_retention_sweeper.h"
#include "message_edit_manager.h"
#include "message_handlers.h"
#include "message_migration_worker.h"
#include "network/device_presence.h"
#include "npc_uplink.h"
#include "paginated_history_retriever.h"
#include "player_directory.h"
#include "push_bridge.h"
#include "word_filter.h"
#include "word_filter_push.h"

#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"

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
#include "enhanced_dispatch.h"

namespace {

using chirp::chat::HybridMessageStore;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::PaginatedHistoryRetriever;
using chirp::chat::MessageStoreConfig;
using chirp::common::Logger;
using chirp::chat::enhanced::DispatchClientFeaturePacket;
using chirp::chat::enhanced::DistributedChatState;
using chirp::chat::enhanced::EnhancedRecallRuntime;
using chirp::chat::enhanced::HandleDeleteMessage;
using chirp::chat::enhanced::HandleGetHistory;
using chirp::chat::enhanced::HandleGetHistoryV2;
using chirp::chat::enhanced::HandleLogin;
using chirp::chat::enhanced::HandleSendMessage;
using chirp::chat::enhanced::HandleServerAuth;
using chirp::chat::enhanced::HealthyLocalSessions;
using chirp::chat::enhanced::MakeEnhancedRecallRuntime;
using chirp::chat::enhanced::ToMessageData;
using chirp::chat::enhanced::TrackAckIfCapable;
}  // namespace
int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string host = chirp::chat::runtime::GetArg(argc, argv, "--host", "0.0.0.0");
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
  // 撤回窗口与可撤回频道（game_chat_features P0 消息撤回）：与 basic 构建同一套
  // 参数语义——窗口 0 = 不限；频道名单 fail-closed（解析不出的一律不可撤回）。
  const int recall_window_sec =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--recall_window_sec", 120);
  const std::string recall_channels =
      chirp::chat::runtime::GetArg(argc, argv, "--recall_channels", "private,guild");
  // World-channel level gate (game_chat_features P1, 拍板形态①): off by
  // default so app/companion deployments are unaffected; game deployments
  // opt in (documented recommendation: 10).
  const int world_channel_min_level =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--world_channel_min_level", 0);

  // 历史保留窗（game_chat_features P2 消息过期）：天数计，0 = 关（缺省，
  // app/companion 部署零 I/O）。私聊窗未配时跟随普通窗——「私聊保留更久」
  // 是把它配得更大的旋钮，不是硬规则。
  const int history_retention_days =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--history_retention_days", 0);
  const int private_history_retention_days_raw =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--private_history_retention_days", 0);
  const int private_history_retention_days =
      private_history_retention_days_raw > 0 ? private_history_retention_days_raw
                                             : history_retention_days;

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
  state->world_channel_min_level = world_channel_min_level;

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

  // 消息过期（P2）：保留窗周期清理（双 0 = 关，Start 内部自判零 I/O）。
  auto retention_sweeper = std::make_shared<chirp::chat::HistoryRetentionSweeper>(
      io, store,
      static_cast<int64_t>(history_retention_days) * 24 * 60 * 60 * 1000,
      static_cast<int64_t>(private_history_retention_days) * 24 * 60 * 60 * 1000);
  retention_sweeper->Start();

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  // Offline pushes are wired only when a notification service is configured;
  // a null client makes the bridge a no-op.
  const std::string notification_host = chirp::chat::runtime::GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--notification_port", 5006);
  const std::string notification_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--notification_secret", "");
  // Prometheus scrape endpoint, opt-in: 0 keeps the listening surface
  // unchanged for existing deployments.
  const uint16_t metrics_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--metrics_port", 0);
  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::app_notification::NotificationClient>(
        io, notification_host, notification_port, notification_secret);
  }
  chirp::chat::PushBridge push(notification);

  // Client delivery-ack bookkeeping: live deliveries to ack-capable sessions
  // stay pending until MESSAGE_ACK; the timeout hands them back to the
  // offline queue (no push notification - the receiver was just online).
  chirp::chat::DeliveryAckManager::Config ack_config;
  ack_config.timeout_ms = ack_timeout_ms;
  auto acks = std::make_shared<chirp::chat::DeliveryAckManager>(
      io, ack_config,
      [store](const std::string& receiver_id, const std::string& slot,
              const std::string& payload) {
        store->AddOfflineMessage(receiver_id, slot, payload);
      },
      [store](const std::string& receiver_id, const std::string& slot,
              const std::string& payload) {
        store->RemoveOfflineMessage(receiver_id, slot, payload);
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
          store->StoreMessageAsync(data, [msg](bool ok) {
            if (!ok) {
              Logger::Instance().Error("Inject " + msg.message_id() +
                                       " not persisted after cold-tier retries");
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
      if (!TrackAckIfCapable(acks.get(), healthy, msg.message_id(), msg.delivery_id(),
                             receiver_id, chirp::chat::kDefaultOfflineSlot,
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
          // 入队臂:注入面不知道接收方设备 → default 共享桶。
          store->AddOfflineMessage(user_id, chirp::chat::kDefaultOfflineSlot,
                                   data.SerializeAsString());
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
    // inject_id 幂等窗（表一 P1-4）：hub 在收到 ack 前重投同一个
    // INJECT_MESSAGE_NOTIFY 时，回吐首次落库的 message_id，不重复落库/投递。
    const int64_t inject_idempotency_ttl_ms =
        chirp::chat::runtime::ParseIntArg(argc, argv, "--inject_idempotency_ttl_ms",
                                          chirp::chat::TtlInjectIdempotency::kDefaultTtlMs);
    auto consumer = std::make_shared<chirp::chat::InjectConsumer>(
        std::move(hooks),
        std::make_shared<chirp::chat::TtlInjectIdempotency>(inject_idempotency_ttl_ms));
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
  const std::string presence_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--game_presence_redis_host", "");
  const uint16_t presence_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--game_presence_redis_port", 6379);
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
  // 游戏在线状态（好友消息进游戏）：开关走写透镜像，在线名单与上下线事件发
  // 到这个 Redis（social 平面消费）；不配则纯内存——开关仍生效，只是不对外
  // 发布。
  if (!presence_redis_host.empty()) {
    directory_options.presence_redis = [host = presence_redis_host,
                                        port = presence_redis_port] {
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
        // 副本保留源消息的发送者类型：NPC/系统公告的逐订阅者私有副本不降级
        // 成玩家（proto 扇入承诺即 SENDER_SERVICE 副本）；玩家上行走默认 USER。
        copy.set_sender_kind(notify.message().sender_kind());
        chirp::chat::MessageData data = ToMessageData(copy);
        store->StoreMessageAsync(data, [copy](bool ok) {
          if (!ok) {
            Logger::Instance().Error("Fan-out copy " + copy.message_id() +
                                     " not persisted after cold-tier retries");
          }
        });
        auto healthy = HealthyLocalSessions(state, player_id);
        if (!healthy.empty()) {
          if (!TrackAckIfCapable(acks.get(), healthy, copy.message_id(), copy.delivery_id(),
                                 player_id, chirp::chat::kDefaultOfflineSlot,
                                 copy.SerializeAsString())) {
            delivery_tracker->Acknowledge(copy.message_id(), player_id);
          }
          for (const auto& recv_session : healthy) {
            chirp::chat::runtime::SendChatNotify(recv_session, copy);
          }
        } else {
          store->AddOfflineMessage(player_id, chirp::chat::kDefaultOfflineSlot,
                                   data.SerializeAsString());
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
            store->StoreMessageAsync(data, [msg](bool ok) {
              if (!ok) {
                Logger::Instance().Error("Peer inject " + msg.message_id() +
                                         " not persisted after cold-tier retries");
              }
            });
            auto healthy = HealthyLocalSessions(state, inject.channel_id());
            if (!healthy.empty()) {
              if (!TrackAckIfCapable(acks.get(), healthy, msg.message_id(), msg.delivery_id(),
                                     inject.channel_id(), chirp::chat::kDefaultOfflineSlot,
                                     msg.SerializeAsString())) {
                delivery_tracker->Acknowledge(msg.message_id(), inject.channel_id());
              }
              for (const auto& recv_session : healthy) {
                chirp::chat::runtime::SendChatNotify(recv_session, msg);
              }
              Logger::Instance().Info("peer inject delivered live to " + inject.channel_id());
            } else {
              store->AddOfflineMessage(inject.channel_id(), chirp::chat::kDefaultOfflineSlot,
                                       data.SerializeAsString());
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
  // 词库热更新广播（docs/design-notes/word_filter.md「词库下发协议」）：每次
  // LoadLexicon（含热重载）推新词库给全部活会话，与 basic 同一接线。
  word_filter.set_on_reload([&state, &word_filter](int64_t) {
    chirp::chat::BroadcastWordFilterUpdate(state->registry, word_filter);
  });
  // 撤回（game_chat_features P0）：enhanced 形态的 DELETE_MESSAGE 入口。窗口与
  // 频道名单与 basic 同一套 flag；装配体（notify/members/moderator/marker/
  // purger 五回调）见 MakeEnhancedRecallRuntime，main 与单测共用同一份。
  // 群组台账（basic 同一 GroupManager，内存态）：群 opcode（建群/邀请/退群/
  // 踢人/名单）、群消息成员扇出、typing/已读/表情的群频道成员解析共用。
  // 与 basic 同样是单实例台账；跨实例成员通知与既有 notify 一致是尽力而为。
  chirp::chat::GroupManager groups;
  // 临时频道过期清扫（P1）：周期回收到点的临时群（读路径惰性摘除只覆盖
  // 被读到的那些）。与保留清扫同一 io context，Start 内部零 I/O 兜底。
  chirp::chat::GroupExpirySweeper group_expiry_sweeper(io, groups);
  group_expiry_sweeper.Start();
  // 举报台账（basic 同一 ReportLedger，内存态）：REPORT_MESSAGE 2253 的记录面，
  // 存在性校验借撤回管道的编辑台账。
  chirp::chat::ReportLedger reports;
  chirp::chat::EditConfig recall_edit_config;
  recall_edit_config.recall_time_window_ms =
      recall_window_sec > 0 ? static_cast<int64_t>(recall_window_sec) * 1000 : 0;
  recall_edit_config.recall_channel_types = chirp::chat::ParseChannelTypeList(recall_channels);
  EnhancedRecallRuntime recall_runtime =
      MakeEnhancedRecallRuntime(state, store, recall_edit_config, &groups);
  chirp::chat::ChannelPacer channel_pacer;
  chirp::chat::RepeatGuard repeat_guard;
  // Per-user push filters (game_chat_features P0 频道屏蔽). Referenced by the
  // mute handlers below; lives as long as main, like channel_pacer.
  chirp::chat::DeliveryPrefs delivery_prefs;
  // 防骚扰好友判定（game_chat_features P2，2255-2258）：与 basic 同一来源——
  // social 平面落在同一 Redis 的好友快照；无 Redis 时好友半边按「非好友」
  // 收紧，同群半边照常生效。
  std::shared_ptr<chirp::network::RedisClient> friend_redis;
  if (!redis_host.empty()) {
    friend_redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }
  chirp::chat::FriendGraph friend_graph(friend_redis);

  // Read receipts (basic parity): in-memory cursor + broadcast MESSAGE_READ_NOTIFY
  // to the other members — 私聊解析对端、群频道查群台账（群已读随群台账生效）。
  chirp::chat::ReadReceiptManager receipts;
  chirp::chat::ChannelMemberResolver resolve_members =
      [&groups](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                const std::string& exclude_user_id) -> std::vector<std::string> {
    if (channel_type == chirp::chat::PRIVATE) {
      const size_t sep = channel_id.find('|');
      if (sep == std::string::npos || sep == 0 || sep + 1 >= channel_id.size()) {
        return {};
      }
      std::string left = channel_id.substr(0, sep);
      std::string right = channel_id.substr(sep + 1);
      const std::string& other = (left == exclude_user_id) ? right : left;
      if (other.empty() || other == exclude_user_id) {
        return {};
      }
      return {other};
    }
    // 群频道（桌面侧 GUILD 型 channel_id=群 id）：按成员名单广播，排除发起
    // 者。台账外频道（游戏平面）返回空。
    std::vector<std::string> out;
    for (const auto& member : groups.GetMembers(channel_id)) {
      if (member.user_id() != exclude_user_id) {
        out.push_back(member.user_id());
      }
    }
    return out;
  };
  chirp::chat::UserNotifier notify_member =
      [state, &delivery_prefs](const std::string& user_id,
                               chirp::gateway::MsgID msg_id,
                               const google::protobuf::Message& body) -> bool {
    if (msg_id == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
      const auto& chat_msg = static_cast<const chirp::chat::ChatMessage&>(body);
      if (delivery_prefs.IsChannelMuted(user_id, chat_msg.channel_type()) ||
          delivery_prefs.IsUserBlocked(user_id, chat_msg.sender_id())) {
        return true;
      }
    }
    const auto recvs = chirp::network::GetUserSessions(state->registry, user_id);
    if (recvs.empty()) {
      return false;
    }
    const std::string payload = body.SerializeAsString();
    for (const auto& recv : recvs) {
      chirp::chat::runtime::SendPacket(recv, msg_id, 0, payload);
    }
    return true;
  };
  chirp::chat::ReadReceiptHandlers receipt_handlers(receipts, resolve_members, notify_member);
  chirp::chat::GroupHandlers group_handlers(groups, notify_member);
  chirp::chat::ReportHandlers report_handlers(reports, *recall_runtime.edits);
  // Typing（2208 notify 族 + 2223 查询）与表情（2215-2220）：与 basic 同一套
  // handlers 单元，成员解析与通知都落在群台账/私聊规范 id 上。
  chirp::chat::TypingConfig typing_config;
  chirp::chat::TypingManager typing(typing_config);
  chirp::chat::TypingHandlers typing_handlers(typing, resolve_members, notify_member);
  chirp::chat::ReactionManager reactions;
  chirp::chat::ReactionHandlers reaction_handlers(reactions, resolve_members, notify_member);

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router, &token_verifier, acks, &delivery_prefs](
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::auth::LoginRequest& req,
                          int64_t seq) {
    HandleLogin(req, session, state, store, router, &token_verifier, acks.get(),
                &delivery_prefs, seq);
  };
  handlers.on_send_message = [state, store, delivery_tracker, acks, router,
                              peer = hub_peer.get(), npc_service_id, npc_prefix,
                              link = spoke_link.get(), spoke_game_id,
                              hub = chat_hub.get(), &directory, &word_filter,
                              &channel_pacer, &repeat_guard,
                              delivery_prefs_ptr = &delivery_prefs,
                              send_gate = &edge_rate_limiter,
                              edit_handlers = recall_runtime.handlers.get(),
                              &group_handlers, &reaction_handlers,
                              &friend_graph](
                                 const std::shared_ptr<chirp::network::Session>& session,
                                 const chirp::chat::SendMessageRequest& req,
                                 int64_t seq) {
    // Per-channel content cap (game_chat_features P0 长度限制) — checked
    // before the filter so over-long messages are refused without spending
    // lexicon work. Same shared validation the basic form gets for free;
    // the shared validator reports the dedicated CONTENT_TOO_LONG code.
    if (const chirp::common::ErrorCode length_code = chirp::chat::ValidateContentLength(req);
        length_code != chirp::common::OK) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(length_code);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                       resp.SerializeAsString());
      return;
    }
    const std::string authenticated_user = state->GetUserId(session);
    // World-channel level gate (game_chat_features P1, 拍板形态① 登录声明):
    // declared level must reach --world_channel_min_level (0 = off). Sits
    // with the identity-class gates — a below-level send consumes neither
    // send budget, pacing slot, repeat-guard state nor filter work, and
    // unauthenticated sessions keep their AUTH_FAILED path untouched.
    if (!authenticated_user.empty() && state->world_channel_min_level > 0 &&
        req.channel_type() == chirp::chat::WORLD &&
        state->GetLevel(session) < state->world_channel_min_level) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::LEVEL_REQUIRED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, seq,
                                       resp.SerializeAsString());
      return;
    }
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
    // with the dedicated WORD_FILTERED code.
    chirp::chat::SendMessageRequest working = req;
    if (word_filter.enabled()) {
      std::string filtered = working.content();
      if (!word_filter.Filter(working.sender_id(), &filtered)) {
        chirp::chat::SendMessageResponse resp;
        resp.set_code(chirp::common::WORD_FILTERED);
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
    HandleSendMessage(working, session, state, store, delivery_tracker, acks.get(),
                      delivery_prefs_ptr, router, peer, npc_service_id, npc_prefix, link,
                      spoke_game_id, edit_handlers, &directory, hub, &group_handlers,
                      &reaction_handlers, &friend_graph, seq);
  };
  handlers.on_get_history = [retriever, state](const std::shared_ptr<chirp::network::Session>& session,
                                        const chirp::chat::GetHistoryRequest& req,
                                        int64_t seq) {
    HandleGetHistory(req, session, retriever,
                     chirp::network::SessionSupportsGzip(state->registry, session.get()), seq);
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
    if (acks->Acknowledge(req.message_id(), req.delivery_id())) {
      // The real client receipt also settles the delivery tracker's status.
      delivery_tracker->Acknowledge(req.message_id(), user_id);
      Logger::Instance().Info("message acked id=" + req.message_id() +
                              (req.delivery_id().empty()
                                   ? " (first-delivery subject)"
                                   : " dlv=" + req.delivery_id()) +
                              " user=" + user_id);
    }
  };
  // Channel mutes (game_chat_features P0 频道屏蔽): the same contract as the
  // basic build's SET_CHANNEL_MUTE/GET_CHANNEL_MUTES handlers. Only the
  // world/guild/team channels are muteable — marquee and SYSTEM_CHANNEL are
  // service broadcasts, private chat belongs to the block list.
  handlers.on_set_channel_mute = [state, &delivery_prefs](
                                     const std::shared_ptr<chirp::network::Session>& session,
                                     const chirp::chat::SetChannelMuteRequest& req,
                                     int64_t seq) {
    chirp::chat::SetChannelMuteResponse resp;
    const std::string user_id = state->GetUserId(session);
    if (user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!chirp::chat::IsMuteableChannel(req.channel_type())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
      resp.set_muted(
          delivery_prefs.SetChannelMuted(user_id, req.channel_type(), req.muted()));
    }
    resp.set_channel_type(req.channel_type());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_CHANNEL_MUTE_RESP, seq,
                                     resp.SerializeAsString());
  };
  handlers.on_get_channel_mutes = [state, &delivery_prefs](
                                      const std::shared_ptr<chirp::network::Session>& session,
                                      const chirp::chat::GetChannelMutesRequest&,
                                      int64_t seq) {
    chirp::chat::GetChannelMutesResponse resp;
    const std::string user_id = state->GetUserId(session);
    if (user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      resp.set_code(chirp::common::OK);
      for (const auto& [channel_type, muted] : delivery_prefs.GetChannelMutes(user_id)) {
        chirp::chat::ChannelMuteState* entry = resp.add_states();
        entry->set_channel_type(channel_type);
        entry->set_muted(muted);
      }
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_CHANNEL_MUTES_RESP, seq,
                                     resp.SerializeAsString());
  };
  // 黑名单（game_chat_features P0）：与 basic 构建的 BLOCK/UNBLOCK/
  // GET_BLOCKED_SENDERS 同一契约——空目标与自拉黑拒绝（INVALID_PARAM），
  // 解除幂等；过滤挂在投递尾段（local_send 报"已投递"阻止离线入队、跨实例
  // SubscribeUserChat 回调丢弃）。
  handlers.on_block_message_sender = [state, &delivery_prefs](
                                         const std::shared_ptr<chirp::network::Session>& session,
                                         const chirp::chat::BlockMessageSenderRequest& req,
                                         int64_t seq) {
    chirp::chat::BlockMessageSenderResponse resp;
    const std::string user_id = state->GetUserId(session);
    if (user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!delivery_prefs.BlockUser(user_id, req.target_user_id())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
    }
    resp.set_target_user_id(req.target_user_id());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::BLOCK_MESSAGE_SENDER_RESP, seq,
                                     resp.SerializeAsString());
  };
  handlers.on_unblock_message_sender = [state, &delivery_prefs](
                                           const std::shared_ptr<chirp::network::Session>& session,
                                           const chirp::chat::UnblockMessageSenderRequest& req,
                                           int64_t seq) {
    chirp::chat::UnblockMessageSenderResponse resp;
    const std::string user_id = state->GetUserId(session);
    if (user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!delivery_prefs.UnblockUser(user_id, req.target_user_id())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
    }
    resp.set_target_user_id(req.target_user_id());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::UNBLOCK_MESSAGE_SENDER_RESP, seq,
                                     resp.SerializeAsString());
  };
  handlers.on_get_blocked_senders = [state, &delivery_prefs](
                                        const std::shared_ptr<chirp::network::Session>& session,
                                        const chirp::chat::GetBlockedSendersRequest&,
                                        int64_t seq) {
    chirp::chat::GetBlockedSendersResponse resp;
    const std::string user_id = state->GetUserId(session);
    if (user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      resp.set_code(chirp::common::OK);
      for (const auto& target : delivery_prefs.GetBlockedUsers(user_id)) {
        resp.add_target_user_ids(target);
      }
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_BLOCKED_SENDERS_RESP, seq,
                                     resp.SerializeAsString());
  };
  // 撤回/版主删除（game_chat_features P0）：DELETE_MESSAGE 此前在 enhanced
  // dispatch 里没有槽位——游戏平面部署形态一直没有撤回入口，这里补上。
  handlers.on_delete_message = [state, edit_handlers = recall_runtime.handlers.get()](
                                   const std::shared_ptr<chirp::network::Session>& session,
                                   const chirp::chat::DeleteMessageRequest& req,
                                   int64_t seq) {
    HandleDeleteMessage(req, session, state, *edit_handlers, seq);
  };

  auto on_packet = [handlers, gateway_service_secret, trusted_conns, &edge_rate_limiter,
                    &directory, &word_filter, &receipt_handlers, state, &group_handlers,
                    &typing_handlers, &reaction_handlers, &report_handlers,
                    &delivery_prefs,
                    edit_handlers = recall_runtime.handlers.get()](
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
    CHIRP_COUNTER("chirp_chat_packets_total", 1);
    if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
      HandleServerAuth(pkt, session, gateway_service_secret, trusted_conns.get());
      return;
    }
    // 词库下发（docs/design-notes/word_filter.md「词库下发协议」）：分发器
    // 是槽位制（DistributedDispatchHandlers），FETCH 不进槽位、在这里拦——
    // 未登录拒绝的判定用 registry 的绑定身份，与 distributed 会话语义一致。
    if (pkt.msg_id() == chirp::gateway::WORD_FILTER_FETCH_REQ) {
      chirp::chat::HandleWordFilterFetch(pkt, session, &word_filter,
                                         state->GetUserId(session));
      return;
    }
    // WP-8 player-directory block (5013-5030) rides this port behind the
    // same trust gate: game backends assert bindings/subscriptions here and
    // the app edge self-serves through it.
    if (chirp::chat::DispatchPlayerDirectoryPacket(pkt, session, directory,
                                                   trusted_conns.get())) {
      return;
    }
    // Read receipts (basic parity): MARK_READ_REQ not in distributed dispatch
    // yet; handle inline like WORD_FILTER_FETCH_REQ.
    if (pkt.msg_id() == chirp::gateway::MARK_READ_REQ) {
      chirp::chat::MarkReadRequest req;
      if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        const std::string authenticated_user = state->GetUserId(session);
        auto resp = receipt_handlers.HandleMarkRead(req, authenticated_user);
        chirp::chat::runtime::SendPacket(session, chirp::gateway::MARK_READ_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
      } else {
        chirp::chat::MarkReadResponse resp;
        resp.set_code(chirp::common::INVALID_PARAM);
        chirp::chat::runtime::SendPacket(session, chirp::gateway::MARK_READ_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
      }
      return;
    }
    // 群组/输入中/表情/未读/编辑客户端面 opcode（TODO 2026-10-02 更正注的补
    // 齐项；与 MARK_READ 同族缺口）：DispatchDistributedPacket 槽位制装不下
    // 这批，与 MARK_READ/WORD_FILTER_FETCH 同一拦截位就地处理。
    if (DispatchClientFeaturePacket(pkt, session, state->GetUserId(session),
                                    &group_handlers, &typing_handlers,
                                    &reaction_handlers, &receipt_handlers,
                                    edit_handlers, &report_handlers,
                                    &delivery_prefs)) {
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

  auto server = chirp::chat::runtime::MakeDistributedTcpServer(io, port, on_packet, tcp_disconnect, host);
  auto ws_server = chirp::chat::runtime::MakeDistributedWsServer(io, ws_port, on_packet, ws_disconnect, host);

  server->Start();
  ws_server->Start();
  acks->Start();

  // Minimal metrics endpoint (chirp_common MetricsHttpServer, default handler
  // serves SimpleMetrics in Prometheus text format). Declared after io so it
  // is destroyed first; a failed bind logs and continues without metrics.
  std::optional<chirp::common::MetricsHttpServer> metrics_server;
  if (metrics_port > 0) {
    metrics_server.emplace(io, metrics_port);
    if (metrics_server->Start()) {
      Logger::Instance().Info("Metrics endpoint listening on TCP:" +
                              std::to_string(metrics_port) + " (/metrics)");
    } else {
      Logger::Instance().Warn("Metrics endpoint failed to bind TCP:" +
                              std::to_string(metrics_port) + "; continuing without metrics");
      metrics_server.reset();
    }
  }

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
    retention_sweeper->Stop();
    group_expiry_sweeper.Stop();
    if (metrics_server) {
      metrics_server->Stop();
    }
    io.stop();
  });
  io.run();
  Logger::Instance().Info("chirp_chat_enhanced exited");
  return 0;
}
