#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "identity_registry.h"
#include "network/session.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "subscription_registry.h"
#include "unread_ledger.h"

namespace chirp::chat {

// App-plane player directory (WP-8): the player-identity layer that used to
// live in game_server_gateway, relocated into app_chat. It composes the three
// registries — identity bindings (slice 1), channel subscriptions (slice 2)
// and the unread badge ledger (slice 4) — behind two surfaces:
//
//  1. The WP-8 RPCs (5013-5030) on the chat main client port, gated by the
//     same SERVER_AUTH_REQ trust set as every other internal-plane dial
//     (DispatchPlayerDirectoryPacket below). Game backends assert bindings
//     and backend-owned subscriptions there; the app edge self-serves with
//     empty ids.
//  2. The hub fan-out tail: every CHANNEL_MESSAGE_NOTIFY uplink from a
//     game_chat spoke resolves to the channel's subscribers here, one
//     private-copy handoff per subscriber (the chat main owns the actual
//     delivery — online push, offline queue, ack tracking) plus one unread
//     increment per copy (TODO 55 + 57).
//
// The registries keep their own thread safety; the directory itself is
// single-threaded like the rest of the chat handlers.
class PlayerDirectory {
 public:
  // One private-copy handoff per subscriber of the uplinked channel. The
  // receiver builds the actual private copy (canonical channel id, receiver
  // pinning) and runs the standard delivery tail; being handed the copy
  // means it was accepted by the plane, which is exactly when the badge
  // increments.
  using CopyDeliverer =
      std::function<void(const std::string& player_id, const gateway::ChannelMessageNotify& notify)>;

  struct Options {
    // Bound on fan-out copies per uplink. A channel with more subscribers
    // than this is dropped with a warning instead of fanning out (same
    // contract the old FanoutInject enforced on injects).
    size_t max_fanout_per_message = 10000;
    IdentityRegistry::RedisFactory identities_redis;
    SubscriptionRegistry::RedisFactory subscriptions_redis;
    UnreadLedger::RedisFactory unread_redis;
    // Null keeps the fan-out tail a no-op (directory still serves the RPCs).
    CopyDeliverer deliver_copy;
  };

  explicit PlayerDirectory(Options options);

  // Pulls persisted bindings/subscriptions/counters into memory before the
  // hub starts serving. Redis-unavailable is not fatal (each registry logs
  // and keeps running memory-only).
  void LoadAll();

  // WP-8 slice 1: player identity bindings asserted by the game backend.
  game_server_gateway::BindPlayerIdentityResponse HandleBindPlayerIdentity(
      const game_server_gateway::BindPlayerIdentityRequest& req);
  game_server_gateway::UnbindPlayerIdentityResponse HandleUnbindPlayerIdentity(
      const game_server_gateway::UnbindPlayerIdentityRequest& req);
  game_server_gateway::GetPlayerIdentitiesResponse HandleGetPlayerIdentities(
      const game_server_gateway::GetPlayerIdentitiesRequest& req) const;
  game_server_gateway::ResolveGameUserResponse HandleResolveGameUser(
      const game_server_gateway::ResolveGameUserRequest& req) const;

  // WP-8 slice 2: player channel subscriptions. The same messages serve the
  // backend-asserted path (with a subscription_id idempotency key) and the
  // app edge's self-service path (empty id — the registry mints one after
  // app_gateway pinned player_id to the authenticated user).
  game_server_gateway::SubscribePlayerChannelResponse HandleSubscribePlayerChannel(
      const game_server_gateway::SubscribePlayerChannelRequest& req);
  game_server_gateway::UnsubscribePlayerChannelResponse HandleUnsubscribePlayerChannel(
      const game_server_gateway::UnsubscribePlayerChannelRequest& req);
  game_server_gateway::GetPlayerSubscriptionsResponse HandleGetPlayerSubscriptions(
      const game_server_gateway::GetPlayerSubscriptionsRequest& req) const;

  // WP-8 slice 4: the unified unread badge ledger. Marking read clears badge
  // counters (layered selector, idempotent); the summary reports the
  // player's nonzero (game, channel) counters. Independent of the chat
  // service's read cursors.
  game_server_gateway::MarkChannelsReadResponse HandleMarkChannelsRead(
      const game_server_gateway::MarkChannelsReadRequest& req);
  game_server_gateway::GetUnreadSummaryResponse HandleGetUnreadSummary(
      const game_server_gateway::GetUnreadSummaryRequest& req) const;

  // WP-8 slice 3, hub side: one private-copy handoff per subscriber of
  // (game_id, channel_id) plus one unread increment per copy. Returns the
  // number of copies handed out (0 for no subscribers or over the fan-out
  // bound — both are semantic no-ops, the uplink is best-effort).
  size_t FanoutChannelMessage(const gateway::ChannelMessageNotify& notify);

  // Cross-plane reply (TODO 56): an App player's send whose channel_id is
  // "<game_id>:<bare>" is a reply into the game plane. The directory parses
  // the prefix (game ids cannot contain ':', so the first colon separates
  // them), resolves the sender's game_user_id, and hands a
  // PeerInjectMessageNotify to the game_chat spoke that registered the
  // game_id. The two plane edges are injected by the caller: resolve asks
  // the hub which live spoke serves a game_id ("" = none), inject hands the
  // built notify to that spoke (false = refused).
  //
  // kNoGamePrefix means "not a cross-plane send" — the caller falls through
  // to the ordinary local-channel path. Every other outcome answers the
  // client with its mapped error code, so a refused reply never masquerades
  // as a delivered one. A channel id containing ':' is therefore reserved
  // for the game plane on this port (deployments must not name App-side
  // channels with a colon).
  enum class GameReplyOutcome {
    kNoGamePrefix,   // no usable "<game_id>:<bare>" prefix — not ours
    kUnknownGame,    // prefix parses, but no live spoke registered that game
    kUnboundPlayer,  // the sender holds no binding for that game
    kSent,           // handed to the spoke (the spoke owns delivery from here)
    kSendFailed,     // the hub refused the downlink (peer dropped mid-flight)
  };
  using GameServiceResolver = std::function<std::string(const std::string& game_id)>;
  using GameReplySender = std::function<bool(
      const std::string& service_id, const gateway::PeerInjectMessageNotify& notify)>;

  GameReplyOutcome RelayGameReply(const std::string& sender_player_id,
                                  const std::string& channel_id, const std::string& content,
                                  const std::string& client_msg_id,
                                  const GameServiceResolver& resolve,
                                  const GameReplySender& inject) const;

 private:
  Options options_;
  IdentityRegistry identities_;
  SubscriptionRegistry subscriptions_;
  UnreadLedger unread_;
};

// Serves the WP-8 RPC block (5013-5030) on a chat main client port. Shared
// by both chat forms: call it before the ordinary client dispatch, ideally
// right after the SERVER_AUTH_REQ case. A packet outside the block is left
// alone (returns false). Packets from a connection that has not passed the
// SERVER_AUTH_REQ trust gate are answered AUTH_FAILED — the callers are
// backend services, a loud refusal beats a silent drop.
bool DispatchPlayerDirectoryPacket(
    const gateway::Packet& pkt, const std::shared_ptr<network::Session>& session,
    PlayerDirectory& directory,
    const std::unordered_set<const network::Session*>* trusted_conns);

}  // namespace chirp::chat
