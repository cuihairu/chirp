#ifndef CHIRP_CHAT_BASIC_PACKET_HANDLER_H_
#define CHIRP_CHAT_BASIC_PACKET_HANDLER_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "basic_message_store.h"
#include "channel_pacer.h"
#include "chat_rate_limiter.h"
#include "delivery_ack_manager.h"
#include "delivery_prefs.h"
#include "friend_graph.h"
#include "group_handlers.h"
#include "login_token_verifier.h"
#include "message_handlers.h"
#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/server_gateway_peer.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "player_directory.h"
#include "push_bridge.h"
#include "repeat_guard.h"
#include "word_filter.h"

namespace chirp::chat::basic {

// Aggregate of the per-feature request handlers wired into the dispatch.
struct FeatureHandlers {
  chirp::chat::GroupHandlers& groups;
  chirp::chat::ReadReceiptHandlers& receipts;
  chirp::chat::TypingHandlers& typing;
  chirp::chat::ReactionHandlers& reactions;
  chirp::chat::MessageEditHandlers& edits;
  chirp::chat::MentionHandlers& mentions;
  chirp::chat::ReportHandlers& reports;
  chirp::chat::PushBridge& push;
  // NPC uplink: peer is null unless --server_gateway_host is set; an empty
  // npc_service_id keeps the feature off even with a live peer.
  chirp::network::ServerGatewayPeer* hub_peer = nullptr;
  std::string npc_service_id;
  std::string npc_prefix = "npc:";
  // Direct-entry abuse gate: null keeps the legacy unthrottled behavior (not
  // expected in practice — main always installs one; it fails open without
  // Redis).
  chirp::chat::ChatRateLimiter* rate_limiter = nullptr;
  // Per-channel pacing (world 5s / guild 2s / private 1s); always installed.
  chirp::chat::ChannelPacer* channel_pacer = nullptr;
  // Repeat-message mute (3 identical sends -> 5 min); always installed.
  chirp::chat::RepeatGuard* repeat_guard = nullptr;
  // Per-user push filters (channel mutes); always installed.
  chirp::chat::DeliveryPrefs* delivery_prefs = nullptr;
  // Lexicon content filter; a null/empty-lexicon filter is a pass-through.
  chirp::chat::WordFilter* word_filter = nullptr;
  // Null or disabled() keeps the scaffold "token is user_id" login.
  const chirp::common::LoginTokenVerifier* token_verifier = nullptr;
  // Client delivery-ack bookkeeping; null (or a disabled manager) keeps the
  // send-and-forget delivery for every session.
  chirp::chat::DeliveryAckManager* acks = nullptr;
  // Trust gate for internal-plane dials (the gateway's per-client pipes).
  // Empty secret keeps SERVER_AUTH_REQ ignored — direct entry only, exactly
  // the historical behavior. Trusted connections skip the per-IP login
  // limiter only; token verification still applies.
  std::string gateway_secret;
  std::shared_ptr<std::unordered_set<const chirp::network::Session*>> trusted_conns;
  // Spoke-mode uplink to the app-plane chat hub (game_chat deployment).
  // Null = spoke disabled; uplink sends through it are best-effort and drop
  // while the link is unregistered — the game plane keeps running.
  std::shared_ptr<chirp::network::ChatPeerLink> spoke = nullptr;
  // Namespace the uplink stamps on CHANNEL_MESSAGE_NOTIFY (the hub prefixes
  // its channels with it); empty when the spoke is off.
  std::string spoke_game_id = {};
  // App-plane player directory (WP-8): identity bindings, subscriptions,
  // unread ledger and the hub fan-out tail. Main always installs one; the
  // RPC block on this port answers only trusted (SERVER_AUTH_REQ) dials.
  chirp::chat::PlayerDirectory* directory = nullptr;
  // The hub side of the peer protocol (app_chat deployment, --hub_mode);
  // null when hub mode is off. The cross-plane reply path resolves the
  // channel's "<game_id>:" prefix against its registrations.
  chirp::network::ChatPeerHub* hub = nullptr;
  // World-channel level gate (game_chat_features P1, 2026-10-11 拍板形态①
  // 登录声明): 0 keeps the gate off (default — app/无等级部署不受影响);
  // N >= 1 rejects world-channel sends from sessions whose LoginRequest
  // declared a lower level with LEVEL_REQUIRED. The declared level rides
  // the session registry (session_to_level), same trust class as the
  // scaffolding identity.
  int world_channel_min_level = 0;
  // 防骚扰好友判定（game_chat_features P2，2255-2258）：null 时私聊门的
  // 好友半边按「非好友」处理（无 Redis 部署只剩同群半边生效）。
  chirp::chat::FriendGraph* friend_graph = nullptr;
};


std::string GenerateSessionId();

std::vector<std::shared_ptr<chirp::network::Session>> HealthyUserSessions(
    const std::shared_ptr<chirp::network::SessionRegistry>& state,
    const std::string& user_id);

void TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks,
                       const std::vector<std::shared_ptr<chirp::network::Session>>& sessions,
                       const std::string& message_id, const std::string& delivery_id,
                       const std::string& receiver_id, const std::string& slot,
                       const std::string& payload);

void HandleDisconnect(const std::shared_ptr<chirp::network::SessionRegistry>& state,
                      chirp::chat::DeliveryAckManager* acks,
                      const std::shared_ptr<std::unordered_set<const chirp::network::Session*>>& trusted_conns,
                      const std::shared_ptr<chirp::network::Session>& session);

// Dispatch one client packet: parse, gate, route to the feature handlers.
void HandlePacket(const std::shared_ptr<MessageStore>& store,
                  const std::shared_ptr<chirp::network::SessionRegistry>& state,
                  FeatureHandlers& features,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload);

}  // namespace chirp::chat::basic

#endif  // CHIRP_CHAT_BASIC_PACKET_HANDLER_H_
