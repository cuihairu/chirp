#ifndef CHIRP_CHAT_ENHANCED_DISPATCH_H_
#define CHIRP_CHAT_ENHANCED_DISPATCH_H_

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

namespace chirp::chat::enhanced {

struct DistributedChatState {
  std::shared_ptr<chirp::network::SessionRegistry> registry =
      std::make_shared<chirp::network::SessionRegistry>();

  // Current instance ID
  std::string instance_id;

  // World-channel level gate (game_chat_features P1, 2026-10-11 拍板形态①
  // 登录声明): 0 = off (default); N >= 1 rejects world sends from sessions
  // whose declared level is lower, with LEVEL_REQUIRED, before any budget
  // or store is touched. Assigned from --world_channel_min_level in main().
  int32_t world_channel_min_level = 0;

  // Returns the session previously bound to the same (user, platform) pair
  // so the caller can kick it (多端在线顶号：同 platform 新登录顶旧，跨
  // platform 共存)；null when the slot was free, stale, or this very
  // connection. Device/platform strings are normalized inside the registry.
  std::shared_ptr<chirp::network::Session> AddSession(
      const std::string& user_id, const std::string& device_id,
      const std::string& session_id,
      const std::shared_ptr<chirp::network::Session>& session,
      const std::string& platform, int32_t level = 0,
      bool supports_gzip = false) {
    return chirp::network::BindAuthenticatedSession(registry, user_id, session_id,
                                                    device_id, session, platform,
                                                    level, supports_gzip);
  }

  // Only releases the (user, platform) slot while it still points at THIS
  // session: a newer login for the same pair may already own it, and a stale
  // disconnect (e.g. the send client's late FIN) must not unregister the
  // current session. When the release lands, the user's remaining devices
  // are told this (platform, device) went offline (多端在线清单变更).
  void RemoveSession(const std::shared_ptr<chirp::network::Session>& session) {
    std::string user_id;
    std::string device_id;
    std::string platform;
    const bool released = chirp::network::RemoveAuthenticatedSession(
        registry, session, &user_id, &device_id, &platform);
    if (released && !user_id.empty()) {
      chirp::network::BroadcastDevicePresence(registry, user_id, platform, device_id,
                                              /*online=*/false);
      // Only a true release retires a bound (user, platform) slot; a displaced
      // session lands here with released=false and must not dec (its slot
      // already belongs to the newer login, which skipped the inc).
      CHIRP_GAUGE_DEC("chirp_chat_sessions");
    }
  }

  std::string GetUserId(const std::shared_ptr<chirp::network::Session>& session) {
    return chirp::network::GetAuthenticatedSession(registry, session).user_id;
  }

  // Declared game level (LoginRequest.level, 0 when unset) — world-channel
  // gate input only, never an identity fact.
  int32_t GetLevel(const std::shared_ptr<chirp::network::Session>& session) {
    return chirp::network::GetAuthenticatedSession(registry, session).level;
  }
};


/// @brief 撤回管道的 enhanced 装配（game_chat_features P0「enhanced 形态接入
/// 撤回端点」）。五个回调与 basic 构建同契约，落在共享的 SessionRegistry 与
/// HybridMessageStore 上：
///  - notify：发给该用户在本实例注册的全部会话（多端共存语义下每条设备会话都
///    收到 MESSAGE_DELETED_NOTIFY；跨实例与既有 notify 一致是尽力而为）；
///  - members：私聊从规范 "a|b" 频道 id 解析对端；给了群台账时非私聊频道查
///    群成员（排除发起者），否则返回空（存档墓碑与离线回收仍按 store 生效）；
///  - moderator：enhanced 无角色体系，恒 false——is_hard_delete 一律
///    AUTH_FAILED（fail-closed，版主治理路径留在 basic）；
///  - marker：历史存档双 tier 置位并抹除正文；
///  - purger：按 message_id 回收接收方离线队列里的副本（Redis + 内存回退）。
struct EnhancedRecallRuntime {
  std::unique_ptr<chirp::chat::MessageEditManager> edits;
  std::unique_ptr<chirp::chat::MessageEditHandlers> handlers;
};


std::vector<std::shared_ptr<chirp::network::Session>> HealthyLocalSessions( const std::shared_ptr<DistributedChatState>& state, const std::string& user_id);
bool TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks, const std::vector<std::shared_ptr<chirp::network::Session>>& sessions, const std::string& message_id, const std::string& delivery_id, const std::string& receiver_id, const std::string& slot, const std::string& payload);
chirp::chat::MessageData ToMessageData(const chirp::chat::ChatMessage& msg);
void HandleSendMessage(const chirp::chat::SendMessageRequest& req, const std::shared_ptr<chirp::network::Session>& sender_session, const std::shared_ptr<DistributedChatState>& state, const std::shared_ptr<HybridMessageStore>& store, const std::shared_ptr<MessageDeliveryTracker>& delivery_tracker, chirp::chat::DeliveryAckManager* acks, const chirp::chat::DeliveryPrefs* delivery_prefs, const std::shared_ptr<chirp::network::MessageRouter>& router, chirp::network::ServerGatewayPeer* hub_peer, const std::string& npc_service_id, const std::string& npc_prefix, chirp::network::ChatPeerLink* spoke_link, const std::string& spoke_game_id, chirp::chat::MessageEditHandlers* edit_handlers, chirp::chat::PlayerDirectory* directory, chirp::network::ChatPeerHub* hub, chirp::chat::GroupHandlers* group_handlers, chirp::chat::ReactionHandlers* reaction_handlers, chirp::chat::FriendGraph* friend_graph, int64_t seq);
EnhancedRecallRuntime MakeEnhancedRecallRuntime( const std::shared_ptr<DistributedChatState>& state, const std::shared_ptr<HybridMessageStore>& store, const chirp::chat::EditConfig& edit_config, chirp::chat::GroupManager* groups = nullptr);
void HandleDeleteMessage(const chirp::chat::DeleteMessageRequest& req, const std::shared_ptr<chirp::network::Session>& session, const std::shared_ptr<DistributedChatState>& state, chirp::chat::MessageEditHandlers& edit_handlers, int64_t seq);
bool DispatchClientFeaturePacket( const chirp::gateway::Packet& pkt, const std::shared_ptr<chirp::network::Session>& session, const std::string& authenticated_user, chirp::chat::GroupHandlers* group_handlers, chirp::chat::TypingHandlers* typing_handlers, chirp::chat::ReactionHandlers* reaction_handlers, chirp::chat::ReadReceiptHandlers* receipt_handlers, chirp::chat::MessageEditHandlers* edit_handlers, chirp::chat::ReportHandlers* report_handlers = nullptr, chirp::chat::DeliveryPrefs* delivery_prefs = nullptr);
bool HandleServerAuth(const chirp::gateway::Packet& pkt, const std::shared_ptr<chirp::network::Session>& session, const std::string& secret, std::unordered_set<const chirp::network::Session*>* trusted);
void HandleLogin(const chirp::auth::LoginRequest& req, const std::shared_ptr<chirp::network::Session>& session, const std::shared_ptr<DistributedChatState>& state, const std::shared_ptr<HybridMessageStore>& store, const std::shared_ptr<chirp::network::MessageRouter>& router, const chirp::common::LoginTokenVerifier* token_verifier, chirp::chat::DeliveryAckManager* acks, const chirp::chat::DeliveryPrefs* delivery_prefs, int64_t seq);
void HandleGetHistory(const chirp::chat::GetHistoryRequest& req, const std::shared_ptr<chirp::network::Session>& session, const std::shared_ptr<PaginatedHistoryRetriever>& retriever, bool supports_gzip, int64_t seq, const std::string& authenticated_user_id);
void HandleGetHistoryV2(const std::string& request_body, const std::shared_ptr<chirp::network::Session>& session, int64_t seq);
}  // namespace chirp::chat::enhanced

#endif  // CHIRP_CHAT_ENHANCED_DISPATCH_H_
