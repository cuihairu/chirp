#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "chat_rate_limiter.h"
#include "delivery_ack_manager.h"
#include "history_compression.h"
#include "network/device_presence.h"
#include "network/session_registry.h"
#include "login_token_verifier.h"
#include "chat_validation.h"
#include "group_handlers.h"
#include "inject_consumer.h"
#include "inject_idempotency.h"
#include "logger.h"
#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"
#include "message_handlers.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "recall_tombstone.h"
#include "network/websocket_server.h"
#include "npc_uplink.h"
#include "player_directory.h"
#include "channel_pacer.h"
#include "delivery_prefs.h"
#include "friend_graph.h"
#include "repeat_guard.h"
#include "word_filter.h"
#include "word_filter_push.h"
#include "push_bridge.h"
#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/notification_client.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "runtime_utils.h"
#include "network/server_gateway_peer.h"

#include "basic_message_store.h"
#include "basic_packet_handler.h"

namespace {
using chirp::chat::basic::FeatureHandlers;
using chirp::chat::basic::GenerateSessionId;
using chirp::chat::basic::HandleDisconnect;
using chirp::chat::basic::HandlePacket;
using chirp::chat::basic::HealthyUserSessions;
using chirp::chat::basic::MessageStore;
using chirp::chat::basic::TrackAckIfCapable;
}  // namespace

int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string host = chirp::chat::runtime::GetArg(argc, argv, "--host", "0.0.0.0");
  const uint16_t metrics_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--metrics_port", 0);
  const std::string redis_host = chirp::chat::runtime::GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int offline_ttl_seconds = chirp::chat::runtime::ParseIntArg(argc, argv, "--offline_ttl", 604800);
  const std::string notification_host = chirp::chat::runtime::GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--notification_port", 5006);
  const std::string notification_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--notification_secret", "");
  // Direct-entry abuse controls: per-IP login and per-user send windows
  // (Redis-backed, fail-open). Without --redis_host the limiter is inert.
  const int login_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--login_rate_limit_per_min", 30);
  const int send_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--send_rate_limit_per_min", 120);
  // Empty keeps the scaffold "token is user_id" login; set to an HS256 secret
  // shared with the token issuer to require verifiable, unexpired JWTs.
  // Lexicon content filter (game_chat_features P0 敏感词过滤). Empty path
  // keeps it off entirely.
  const std::string word_filter_file =
      chirp::chat::runtime::GetArg(argc, argv, "--word_filter_file", "");
  const std::string word_filter_policy =
      chirp::chat::runtime::GetArg(argc, argv, "--word_filter_policy", "replace");
  // Message recall (game_chat_features P0 消息撤回): how long a sender may
  // withdraw their own message (seconds, 0 = unlimited) and on which channels
  // (game_chat_features default: private + guild). Moderator removal via
  // DELETE_MESSAGE ignores both.
  const int recall_window_sec =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--recall_window_sec", 120);
  const std::string recall_channels =
      chirp::chat::runtime::GetArg(argc, argv, "--recall_channels", "private,guild");
  // World-channel level gate (game_chat_features P1, 拍板形态①): the gate
  // is off by default so app/companion deployments (no level data) are
  // unaffected; game deployments opt in (documented recommendation: 10).
  const int world_channel_min_level =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--world_channel_min_level", 0);
  const std::string token_secret = chirp::chat::runtime::GetArg(argc, argv, "--token_secret", "");
  // Internal-plane trust gate: when set, edge gateways that dial in with this
  // secret (SERVER_AUTH_REQ) get per-client pipes that skip the per-IP login
  // limiter. Empty keeps SERVER_AUTH_REQ ignored (direct entry only).
  const std::string gateway_service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--gateway_service_secret", "");
  // Hub mode: accept game_chat peer registrations (app_chat deployment).
  // The peer link listens on its own port — the main client port keeps
  // serving the client protocol only.
  const bool hub_mode = chirp::chat::runtime::ParseIntArg(argc, argv, "--hub_mode", 0) != 0;
  const uint16_t hub_peer_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--hub_peer_port", 8200);
  const std::string allowed_peers_str =
      chirp::chat::runtime::GetArg(argc, argv, "--allowed_peers", "");
  const int min_peer_version =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--min_peer_version", 1);
  const bool allow_unknown_peers =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--allow_unknown_peers", 0) != 0;
  // Spoke mode: register into an app_chat hub (game_chat deployment).
  const std::string app_chat_host =
      chirp::chat::runtime::GetArg(argc, argv, "--app_chat_host", "");
  const uint16_t app_chat_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--app_chat_port", 8200);
  const std::string game_service_id =
      chirp::chat::runtime::GetArg(argc, argv, "--game_service_id", "");
  const std::string game_service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--game_service_secret", "");
  const std::string game_id =
      chirp::chat::runtime::GetArg(argc, argv, "--game_id", "");
  Logger::Instance().Info("chirp_chat starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty()
                               ? ""
                               : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                  " offline_ttl=" + std::to_string(offline_ttl_seconds) +
                                  " rate_limit(login/ip/min)=" + std::to_string(login_rate_limit_per_min) +
                                  " rate_limit(send/user/min)=" + std::to_string(send_rate_limit_per_min))) +
                          " auth=" + (token_secret.empty() ? "scaffold" : "hmac-sha256") +
                          (gateway_service_secret.empty() ? "" : " internal-trust=on") +
                          (notification_host.empty()
                               ? ""
                               : (" notification=" + notification_host + ":" + std::to_string(notification_port))));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  // 防骚扰好友判定（game_chat_features P2，2255-2258）：读 social 平面落在
  // 同一 Redis 的好友快照；无 Redis 时好友半边按「非好友」收紧，同群半边
  // 照常生效。
  chirp::chat::FriendGraph friend_graph(redis);

  auto store = std::make_shared<MessageStore>(redis, offline_ttl_seconds);
  auto state = std::make_shared<chirp::network::SessionRegistry>();

  // Client delivery-ack bookkeeping: live deliveries to ack-capable sessions
  // stay pending until MESSAGE_ACK; the timeout hands them back to the
  // offline queue. --ack_timeout_ms 0 disables the feature.
  const int64_t ack_timeout_ms = chirp::chat::runtime::ParseIntArg(argc, argv, "--ack_timeout_ms", 10000);
  chirp::chat::DeliveryAckManager::Config ack_config;
  ack_config.timeout_ms = ack_timeout_ms;
  auto acks = std::make_shared<chirp::chat::DeliveryAckManager>(
      io, ack_config,
      [&store](const std::string& receiver_id, const std::string& slot,
               const std::string& payload) {
        store->AddOfflineBytes(receiver_id, slot, payload);
      },
      [&store](const std::string& receiver_id, const std::string& slot,
               const std::string& payload) {
        store->RemoveOffline(receiver_id, slot, payload);
      });

  chirp::chat::ChatRateLimiter::Config rate_limit_config;
  rate_limit_config.max_logins_per_minute_per_ip = login_rate_limit_per_min;
  rate_limit_config.max_sends_per_minute_per_user = send_rate_limit_per_min;
  auto rate_limiter = std::make_shared<chirp::chat::ChatRateLimiter>(redis, rate_limit_config);
  chirp::common::LoginTokenVerifier token_verifier(token_secret);

  // Offline pushes are only wired when a notification service is configured;
  // a null client turns the bridge into a no-op.
  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::app_notification::NotificationClient>(
        io, notification_host, notification_port, notification_secret);
  }
  chirp::chat::PushBridge push(notification);

  // Per-user push filters (channel mutes); declared before the group
  // notifier because the notifier consults it on every chat broadcast.
  chirp::chat::DeliveryPrefs delivery_prefs;

  // Delivers group notifications to every live session of a member; false
  // means the member has no session right now. Chat-message notifications
  // additionally respect the recipient's channel mutes (game_chat_features
  // P0 频道屏蔽): a muted member reports "delivered" so the broadcast does
  // not queue an offline copy either — the mute is a push filter, history
  // stays browsable. The same notice also respects the recipient's block
  // list（黑名单）: a member who blocked the sender is skipped per member,
  // other members still receive the message. Membership notices (group
  // joined/left/...) ignore mutes and blocks on purpose.
  chirp::chat::GroupMemberNotifier notify_member =
      [state, &delivery_prefs](const std::string& user_id, chirp::gateway::MsgID msg_id,
                               const google::protobuf::Message& body) -> bool {
    if (msg_id == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
      const auto& chat_msg = static_cast<const chirp::chat::ChatMessage&>(body);
      if (delivery_prefs.IsChannelMuted(user_id, chat_msg.channel_type()) ||
          delivery_prefs.IsUserBlocked(user_id, chat_msg.sender_id())) {
        return true;
      }
    }
    const auto recvs = chirp::network::GetUserSessions(state, user_id);
    if (recvs.empty()) {
      return false;
    }
    const std::string payload = body.SerializeAsString();
    for (const auto& recv : recvs) {
      chirp::chat::runtime::SendPacket(recv, msg_id, 0, payload);
    }
    return true;
  };

  chirp::chat::GroupManager groups;
  chirp::chat::GroupHandlers group_handlers(groups, notify_member);

  // Resolves who should receive a channel broadcast: the other party for
  // private channels, the group membership otherwise.
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
    std::vector<std::string> out;
    for (const auto& member : groups.GetMembers(channel_id)) {
      if (member.user_id() != exclude_user_id) {
        out.push_back(member.user_id());
      }
    }
    return out;
  };

  chirp::chat::ReadReceiptManager receipts;
  chirp::chat::TypingConfig typing_config;
  chirp::chat::TypingManager typing(typing_config);
  chirp::chat::ReactionManager reactions;
  chirp::chat::EditConfig edit_config;
  edit_config.recall_time_window_ms =
      recall_window_sec > 0 ? static_cast<int64_t>(recall_window_sec) * 1000 : 0;
  edit_config.recall_channel_types = chirp::chat::ParseChannelTypeList(recall_channels);
  chirp::chat::MessageEditManager edits(edit_config);
  chirp::chat::MentionManager mentions;

  // Resolves moderator rights in a channel: group roles for group-style
  // channels, never for private channels.
  chirp::chat::ChannelModeratorChecker is_moderator =
      [&groups](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                const std::string& user_id) -> bool {
    if (channel_type != chirp::chat::GUILD) {
      return false;
    }
    for (const auto& member : groups.GetMembers(channel_id)) {
      if (member.user_id() == user_id) {
        return member.role() >= chirp::chat::MODERATOR;
      }
    }
    return false;
  };

  chirp::chat::ReadReceiptHandlers receipt_handlers(receipts, resolve_members, notify_member);
  chirp::chat::TypingHandlers typing_handlers(typing, resolve_members, notify_member);
  chirp::chat::ReactionHandlers reaction_handlers(reactions, resolve_members, notify_member);
  // 撤回/删除成功后回收离线副本,否则离线接收方下次登录仍会收到原文。
  chirp::chat::OfflineMessagePurger purge_offline =
      [store](const std::string& message_id, const std::string& receiver_id) {
        store->PurgeOfflineByMessageId(receiver_id, message_id);
      };
  // 撤回墓碑：历史存档（内存 + Redis 镜像）随撤回/软删置 is_recalled。
  chirp::chat::RecallTombstoneMarker mark_recalled =
      [store](chirp::chat::ChannelType channel_type, const std::string& channel_id,
              const std::string& message_id) {
        store->MarkRecalled(channel_type, channel_id, message_id);
      };
  chirp::chat::MessageEditHandlers edit_handlers(edits, resolve_members, is_moderator,
                                                 notify_member, purge_offline,
                                                 mark_recalled);
  chirp::chat::MentionHandlers mention_handlers(mentions, is_moderator);
  // 举报台账（game_chat_features P1「举报」）：存在性校验借编辑台账（只可举报
  // 本实例投递过的消息），记录进进程内存举报台账（按举报人去重）。
  chirp::chat::ReportLedger report_ledger;
  chirp::chat::ReportHandlers report_handlers(report_ledger, edits);

  chirp::chat::WordFilterOptions word_filter_options;
  word_filter_options.lexicon_path = word_filter_file;
  word_filter_options.policy = chirp::chat::WordFilterPolicyFromString(word_filter_policy);
  chirp::chat::WordFilter word_filter(word_filter_options);
  chirp::chat::ChannelPacer channel_pacer;
  chirp::chat::RepeatGuard repeat_guard;
  // 词库下发推送半边：mtime 热更新重载后向全部已认证在线会话广播新词库
  // （回调在 io 线程同步触发，BroadcastWordFilterUpdate 只 post 发送）。
  word_filter.set_on_reload([&state, &word_filter](int64_t) {
    chirp::chat::BroadcastWordFilterUpdate(state, word_filter);
  });

  FeatureHandlers features{.groups = group_handlers, .receipts = receipt_handlers,
                           .typing = typing_handlers, .reactions = reaction_handlers,
                           .edits = edit_handlers, .mentions = mention_handlers,
                           .reports = report_handlers,
                           .push = push, .npc_service_id = {},
                           .gateway_secret = {}, .trusted_conns = nullptr};
  features.rate_limiter = rate_limiter.get();
  features.channel_pacer = &channel_pacer;
  features.repeat_guard = &repeat_guard;
  features.delivery_prefs = &delivery_prefs;
  features.friend_graph = &friend_graph;
  features.word_filter = &word_filter;
  features.token_verifier = &token_verifier;
  features.acks = acks.get();
  features.gateway_secret = gateway_service_secret;
  features.trusted_conns =
      std::make_shared<std::unordered_set<const chirp::network::Session*>>();
  features.world_channel_min_level = world_channel_min_level;

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
  // One private copy per subscriber through the same delivery tail as an
  // ordinary private SEND_MESSAGE: canonical channel id, healthy-device
  // fan-out, ack tracking, offline queue + push when nobody is online.
  directory_options.deliver_copy =
      [&store, &state, &features](const std::string& player_id,
                                  const chirp::gateway::ChannelMessageNotify& notify) {
        chirp::chat::ChatMessage copy = notify.message();
        copy.set_message_id(chirp::chat::runtime::GenerateMessageId());
        // The App client answers to the namespaced game identity — the
        // same "{game_id}:" prefix the cross-plane reply path parses.
        copy.set_sender_id(notify.game_id() + ":" + notify.message().sender_id());
        copy.set_receiver_id(player_id);
        copy.set_channel_type(chirp::chat::PRIVATE);
        copy.set_channel_id(store->PrivateChannelId(copy.sender_id(), player_id));
        // 副本保留源消息的发送者类型：NPC/系统公告的逐订阅者私有副本不降级
        // 成玩家（proto 扇入承诺即 SENDER_SERVICE 副本）；玩家上行走默认 USER。
        copy.set_sender_kind(notify.message().sender_kind());
        store->AddMessage(copy);
        auto receivers = HealthyUserSessions(state, player_id);
        if (!receivers.empty()) {
          TrackAckIfCapable(features.acks, receivers, copy.message_id(),
                            copy.delivery_id(), player_id,
                            chirp::chat::kDefaultOfflineSlot,
                            copy.SerializeAsString());
          for (const auto& recv : receivers) {
            chirp::chat::runtime::SendChatNotify(recv, copy);
          }
        } else {
          store->AddOffline(player_id, chirp::chat::kDefaultOfflineSlot, copy);
          features.push.NotifyOffline(copy, player_id);
        }
      };
  chirp::chat::PlayerDirectory directory(std::move(directory_options));
  directory.LoadAll();
  features.directory = &directory;

  // Hub mode: accept game_chat peer registrations on the dedicated peer
  // port (app_chat deployment). Registration runs on the hub's own
  // acceptor; the main client port keeps serving the client protocol only.
  std::shared_ptr<chirp::network::ChatPeerHub> chat_hub;
  if (hub_mode) {
    chirp::network::ChatPeerHub::Options hub_opts;
    hub_opts.port = hub_peer_port;
    hub_opts.min_peer_version = min_peer_version;
    hub_opts.allow_unknown_peers = allow_unknown_peers;
    // Parse allowed_peers: "id1:secret1,id2:secret2"
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
    // Assign (not declare) so the signal handler below sees the hub and
    // stops it on shutdown — an inner `auto chat_hub = ...` would shadow
    // the outer declaration and leave Stop() a null no-op.
    chat_hub = chirp::network::ChatPeerHub::Create(
        io, std::move(hub_opts),
        [](const std::string& service_id, const std::string& peer_game_id,
           int32_t version,
           const std::vector<chirp::gateway::PeerCapability>&) {
          Logger::Instance().Info("peer registered: service_id=" + service_id +
                                  " game_id=" + peer_game_id +
                                  " version=" + std::to_string(version));
        },
        [](const std::string& service_id, const std::string& reason) {
          Logger::Instance().Info("peer dropped: service_id=" + service_id +
                                  " reason=" + reason);
        },
        [&directory](const std::string& service_id,
                     const chirp::gateway::ChannelMessageNotify& notify) {
          // Cross-plane fan-out (TODO 55): resolve the channel's
          // subscribers in the player directory and hand each one a
          // private copy (plus one unread badge increment per copy).
          const size_t copies = directory.FanoutChannelMessage(notify);
          Logger::Instance().Info(
              "channel message uplink: service_id=" + service_id +
              " game_id=" + notify.game_id() +
              " channel=" + notify.channel_id() +
              " copies=" + std::to_string(copies));
        });
    chat_hub->Start();
    features.hub = chat_hub.get();
    Logger::Instance().Info("hub mode enabled: peer_port=" +
                            std::to_string(chat_hub->port()) +
                            " min_version=" + std::to_string(min_peer_version));
  }

  // Spoke mode: register into an app_chat hub (game_chat deployment).
  // Needs host + service_id + game_id all set; anything missing keeps the
  // spoke fully off.
  if (!app_chat_host.empty() && !game_service_id.empty() && !game_id.empty()) {
    chirp::network::ChatPeerLink::Options link_opts;
    link_opts.host = app_chat_host;
    link_opts.port = app_chat_port;
    link_opts.service_id = game_service_id;
    link_opts.secret = game_service_secret;
    link_opts.game_id = game_id;
    link_opts.supported_features = {chirp::gateway::RELAY_READ_RECEIPTS,
                                    chirp::gateway::RELAY_TYPING,
                                    chirp::gateway::RELAY_PRESENCE};
    features.spoke = chirp::network::ChatPeerLink::Create(
        io, std::move(link_opts),
        [game_id](int32_t version,
                  const std::vector<chirp::gateway::PeerCapability>&) {
          Logger::Instance().Info("spoke registered to hub: game_id=" + game_id +
                                  " negotiated_version=" + std::to_string(version));
        },
        // A hub never sends CHANNEL_MESSAGE_NOTIFY downstream; leave the
        // handler unset (an unexpected frame is logged by the link).
        nullptr,
        // Hub -> spoke player reply: the hub already resolved
        // player_id -> game_user_id, the spoke never sees player ids.
        // Delivered through the same store + healthy-session tail as a
        // private message, offline queue included.
        [&store, &state](const chirp::gateway::PeerInjectMessageNotify& inject) {
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
          store->AddMessage(msg);
          auto receivers = HealthyUserSessions(state, inject.channel_id());
          for (auto& s : receivers) {
            chirp::chat::runtime::SendPacket(
                s, chirp::gateway::CHAT_MESSAGE_NOTIFY, 0, msg.SerializeAsString());
          }
          if (receivers.empty()) {
            store->AddOffline(inject.channel_id(), chirp::chat::kDefaultOfflineSlot, msg);
          }
        },
        []() {
          Logger::Instance().Warn("spoke lost the hub connection, will retry");
        });
    features.spoke_game_id = game_id;
    features.spoke->Start();
    Logger::Instance().Info("spoke mode enabled: game_id=" + game_id +
                            " -> hub=" + app_chat_host + ":" +
                            std::to_string(app_chat_port));
  }

  // Server-plane injection: when --server_gateway_host is set, chat dials the
  // hub as an internal service and delivers forwarded injections through the
  // same store/deliver tail as SEND_MESSAGE (minus mention enforcement -
  // senders are non-user identities, never membership-checked).
  const std::string hub_host = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_host", "");
  // NPC dialog uplink: player -> NPC private messages become events published
  // to the NPC dialog service over the hub peer. Empty --npc_service_id keeps
  // the feature off entirely.
  const std::string npc_service_id =
      chirp::chat::runtime::GetArg(argc, argv, "--npc_service_id", "");
  const std::string npc_prefix =
      chirp::chat::runtime::GetArg(argc, argv, "--npc_prefix", "npc:");
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
        [&store](const std::string& a, const std::string& b) {
          return store->PrivateChannelId(a, b);
        };
    hooks.store_message =
        [&store, &features](const chirp::chat::ChatMessage& msg) {
          store->AddMessage(msg);
          features.receipts.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.reactions.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.edits.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.edits.RegisterMessage(msg.message_id(), msg.sender_id(), msg.content());
        };
    hooks.deliver_private =
        [state, &features](const std::string& receiver_id,
                const chirp::chat::ChatMessage& msg) -> bool {
      const auto receivers = HealthyUserSessions(state, receiver_id);
      if (receivers.empty()) {
        return false;
      }
      // Injected private replies are tracked like SEND_MESSAGE deliveries,
      // then fanned out to every device of the receiver.
      TrackAckIfCapable(features.acks, receivers, msg.message_id(),
                        msg.delivery_id(), receiver_id,
                        chirp::chat::kDefaultOfflineSlot,
                        msg.SerializeAsString());
      for (const auto& recv : receivers) {
        chirp::chat::runtime::SendChatNotify(recv, msg);
      }
      return true;
    };
    hooks.queue_offline =
        [&store, &features](const std::string& user_id, const chirp::chat::ChatMessage& msg) {
          // 入队臂:注入面不知道接收方设备,副本落 default 共享桶。
          store->AddOffline(user_id, chirp::chat::kDefaultOfflineSlot, msg);
          features.push.NotifyOffline(msg, user_id);
        };
    hooks.broadcast_channel =
        [&features](const std::string& channel_id,
                    const chirp::chat::ChatMessage& msg) {
          return features.groups.BroadcastGroupMessage(channel_id, msg.sender_id(), msg);
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
    features.hub_peer = hub_peer.get();
    features.npc_service_id = npc_service_id;
    features.npc_prefix = npc_prefix;
    Logger::Instance().Info("server-plane peer enabled hub=" + hub_host + ":" +
                            std::to_string(hub_port) + " service=" + hub_service +
                            (npc_service_id.empty()
                                 ? std::string()
                                 : " npc_service=" + npc_service_id));
  }

  chirp::network::TcpServer server(
      io, port,
      [store, state, &features](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, features, session, std::move(payload));
      },
      [state, &features](std::shared_ptr<chirp::network::Session> session) {
        HandleDisconnect(state, features.acks, features.trusted_conns, session);
      },
      host);

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [store, state, &features](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, features, session, std::move(payload));
      },
      [state, &features](std::shared_ptr<chirp::network::Session> session) {
        HandleDisconnect(state, features.acks, features.trusted_conns, session);
      },
      host);

  server.Start();
  ws_server.Start();
  acks->Start();

  // Minimal metrics endpoint, opt-in via --metrics_port (0 = off; default
  // keeps the listening surface unchanged). Declared after io so it dies
  // first; a failed bind logs and the service continues without metrics.
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

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    acks->Stop();
    if (features.spoke) {
      features.spoke->Stop();
    }
    if (chat_hub) {
      chat_hub->Stop();
    }
    server.Stop();
    ws_server.Stop();
    if (metrics_server) {
      metrics_server->Stop();
    }
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat exited");
  return 0;
}
