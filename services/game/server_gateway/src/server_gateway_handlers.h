#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "event_queue.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "service_registry.h"

namespace chirp::game_server_gateway {

struct ServerGatewayConfig {
  // service_id -> shared secret; unknown service ids are rejected.
  std::map<std::string, std::string> service_secrets;
  // Message injections are always routed to this internal service.
  std::string chat_service_id = "chat";
  int protocol_version = 1;
  int heartbeat_interval_seconds = 30;
  size_t max_pending_events_per_service = 1000;
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
//
// The WP-8 player layer (identity bindings, channel subscriptions, unread
// ledger and channel fan-out) moved into app_chat (services/shared/chat,
// player_directory); this hub keeps injections + events only.
class ServerGatewayHandlers {
 public:
  ServerGatewayHandlers(ServerGatewayConfig config, ServiceRegistry& registry, EventQueue& queue);

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

  // Cleans up after a connection drops. `peer` must be the connection that
  // owned `service_id`; a displaced (replaced) connection closing late is a
  // no-op so it cannot reset the live connection's in-flight tracking.
  void OnPeerDisconnected(const std::string& service_id, const PeerSender* peer);

 private:
  void DeliverPending(const std::string& service_id, PeerSender& peer);
  std::string GenerateEventId();

  ServerGatewayConfig config_;
  ServiceRegistry& registry_;
  EventQueue& queue_;
  uint64_t event_id_counter_ = 0;
};

}  // namespace chirp::game_server_gateway
