#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "event_queue.h"
#include "identity_registry.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"
#include "service_registry.h"
#include "subscription_registry.h"
#include "unread_ledger.h"

namespace chirp::server_gateway {

struct ServerGatewayConfig {
  // service_id -> shared secret; unknown service ids are rejected.
  std::map<std::string, std::string> service_secrets;
  // Message injections are always routed to this internal service.
  std::string chat_service_id = "chat";
  int protocol_version = 1;
  int heartbeat_interval_seconds = 30;
  size_t max_pending_events_per_service = 1000;
  // Per-inject bound on fan-out copies (WP-8 slice 3). A channel with more
  // subscribers than this rejects the injection instead of fanning out.
  size_t max_fanout_per_inject = 10000;
};

struct AuthOutcome {
  chirp::common::ErrorCode code = chirp::common::AUTH_FAILED;
  std::string service_id;
  int heartbeat_interval_seconds = 0;
  // The connection this login displaced, if any. The caller must close it.
  std::shared_ptr<PeerSender> replaced_peer;
};

// Packet handlers for the server plane. Routing decisions run through the
// injectable ServiceRegistry/EventQueue so every branch is unit-testable
// without sockets. Handlers are invoked from the single io thread; the
// registry and the queue guard their own state for cross-thread readers.
class ServerGatewayHandlers {
 public:
  ServerGatewayHandlers(ServerGatewayConfig config, ServiceRegistry& registry, EventQueue& queue,
                        IdentityRegistry& identities, SubscriptionRegistry& subscriptions,
                        UnreadLedger& unread);

  // Validates credentials and binds the peer to its service id. A returning
  // service immediately receives every event it has not acknowledged yet.
  AuthOutcome HandleAuth(const ServerAuthRequest& req, std::shared_ptr<PeerSender> peer);

  ServerHeartbeatPong HandleHeartbeat(const ServerHeartbeatPing& ping) const;

  // Validates an injected message and forwards it to the chat service.
  MessageInjectResponse HandleInject(const MessageInjectRequest& req) const;

  // Enqueues an event for its target service, delivering immediately when
  // the target is connected.
  EventPublishResponse HandleEventPublish(const EventPublishRequest& req);

  // Acknowledges delivered events; unknown ids are ignored.
  EventAckResponse HandleEventAck(const EventAckRequest& req, const std::string& service_id) const;

  // WP-8 slice 1: player identity bindings asserted by the game backend.
  BindPlayerIdentityResponse HandleBindPlayerIdentity(const BindPlayerIdentityRequest& req);
  UnbindPlayerIdentityResponse HandleUnbindPlayerIdentity(const UnbindPlayerIdentityRequest& req);
  GetPlayerIdentitiesResponse HandleGetPlayerIdentities(const GetPlayerIdentitiesRequest& req) const;
  ResolveGameUserResponse HandleResolveGameUser(const ResolveGameUserRequest& req) const;

  // WP-8 slice 2: player channel subscriptions. The same messages serve the
  // backend-asserted path (with a subscription_id idempotency key) and the
  // app edge's self-service path (empty id — the registry mints one after
  // app_gateway pinned player_id to the authenticated user).
  SubscribePlayerChannelResponse HandleSubscribePlayerChannel(const SubscribePlayerChannelRequest& req);
  UnsubscribePlayerChannelResponse HandleUnsubscribePlayerChannel(const UnsubscribePlayerChannelRequest& req);
  GetPlayerSubscriptionsResponse HandleGetPlayerSubscriptions(const GetPlayerSubscriptionsRequest& req) const;

  // WP-8 slice 4: the unified unread badge ledger. Marking read clears
  // badge counters (layered selector, idempotent); the summary reports the
  // player's nonzero (game, channel) counters. Independent of the chat
  // service's read cursors.
  MarkChannelsReadResponse HandleMarkChannelsRead(const MarkChannelsReadRequest& req);
  GetUnreadSummaryResponse HandleGetUnreadSummary(const GetUnreadSummaryRequest& req) const;

  // Cleans up after a connection drops. `peer` must be the connection that
  // owned `service_id`; a displaced (replaced) connection closing late is a
  // no-op so it cannot reset the live connection's in-flight tracking.
  void OnPeerDisconnected(const std::string& service_id, const PeerSender* peer);

 private:
  // WP-8 slice 3: fan a channel injection out to every subscriber of
  // (game_id, channel_id) as one private copy per subscriber.
  MessageInjectResponse FanoutInject(const MessageInjectRequest& req) const;

  void DeliverPending(const std::string& service_id, PeerSender& peer);
  std::string GenerateEventId();

  ServerGatewayConfig config_;
  ServiceRegistry& registry_;
  EventQueue& queue_;
  IdentityRegistry& identities_;
  SubscriptionRegistry& subscriptions_;
  UnreadLedger& unread_;
  uint64_t event_id_counter_ = 0;
};

}  // namespace chirp::server_gateway
