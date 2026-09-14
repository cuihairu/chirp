#include "server_gateway_handlers.h"

#include <chrono>
#include <utility>
#include <vector>

namespace chirp::server_gateway {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

ServerGatewayHandlers::ServerGatewayHandlers(ServerGatewayConfig config, ServiceRegistry& registry,
                                             EventQueue& queue)
    : config_(std::move(config)), registry_(registry), queue_(queue) {}

AuthOutcome ServerGatewayHandlers::HandleAuth(const ServerAuthRequest& req,
                                              std::shared_ptr<PeerSender> peer) {
  AuthOutcome out;
  if (req.protocol_version() != config_.protocol_version) {
    return out;
  }
  const auto it = config_.service_secrets.find(req.service_id());
  // Plain comparison is acceptable for the server plane: secrets are shared
  // among trusted backend services, not chosen by human users.
  if (it == config_.service_secrets.end() || it->second != req.secret()) {
    return out;
  }

  out.code = chirp::common::OK;
  out.service_id = req.service_id();
  out.heartbeat_interval_seconds = config_.heartbeat_interval_seconds;
  out.replaced_peer = registry_.Register(out.service_id, peer);
  // A returning service immediately gets everything it did not ack before
  // disconnecting.
  DeliverPending(out.service_id, *peer);
  return out;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

ServerHeartbeatPong ServerGatewayHandlers::HandleHeartbeat(const ServerHeartbeatPing& /*ping*/) const {
  ServerHeartbeatPong pong;
  pong.set_server_time_ms(NowMs());
  return pong;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

MessageInjectResponse ServerGatewayHandlers::HandleInject(const MessageInjectRequest& req) const {
  MessageInjectResponse resp;
  resp.set_inject_id(req.inject_id());
  if (req.content().empty() || req.sender_kind() == SENDER_UNKNOWN || req.sender_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (req.receiver_id().empty() && req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  auto chat = registry_.Get(config_.chat_service_id);
  InjectMessageNotify notify;
  *notify.mutable_message() = req;
  if (!chat || !chat->Send(chirp::gateway::INJECT_MESSAGE_NOTIFY, notify)) {
    resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    return resp;
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

EventPublishResponse ServerGatewayHandlers::HandleEventPublish(const EventPublishRequest& req) {
  EventPublishResponse resp;
  if (req.event_type().empty() || req.payload().empty() || req.target_service_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  PendingEvent event;
  event.event_id = req.event_id().empty() ? GenerateEventId() : req.event_id();
  event.event_type = req.event_type();
  event.payload = req.payload();
  event.published_at_ms = NowMs();
  if (!queue_.Enqueue(req.target_service_id(), event)) {
    // A full queue rejects new publishes instead of dropping older events;
    // publishers retry with backoff.
    resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    return resp;
  }

  resp.set_code(chirp::common::OK);
  resp.set_event_id(event.event_id);
  auto target = registry_.Get(req.target_service_id());
  resp.set_queued(target == nullptr);
  if (target) {
    DeliverPending(req.target_service_id(), *target);
  }
  return resp;
}

EventAckResponse ServerGatewayHandlers::HandleEventAck(const EventAckRequest& req,
                                                       const std::string& service_id) const {
  EventAckResponse resp;
  queue_.Ack(service_id,
             std::vector<std::string>(req.event_ids().begin(), req.event_ids().end()));
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

void ServerGatewayHandlers::OnPeerDisconnected(const std::string& service_id,
                                               const PeerSender* peer) {
  // Only the live connection's departure resets in-flight tracking; a
  // displaced connection closing late must not trigger a redelivery storm on
  // the connection that replaced it.
  if (registry_.Unregister(service_id, peer)) {
    queue_.ResetInFlight(service_id);
  }
}

void ServerGatewayHandlers::DeliverPending(const std::string& service_id, PeerSender& peer) {
  for (const auto& event : queue_.ClaimDeliverable(service_id)) {
    EventDeliverNotify notify;
    notify.set_event_id(event.event_id);
    notify.set_event_type(event.event_type);
    notify.set_payload(event.payload);
    notify.set_published_at_ms(event.published_at_ms);
    notify.set_attempt(event.attempt);
    // A failed write means the peer is going away; OnPeerDisconnected will
    // return the event to the queue for redelivery.
    peer.Send(chirp::gateway::EVENT_DELIVER_NOTIFY, notify);
  }
}

std::string ServerGatewayHandlers::GenerateEventId() {
  // Wall clock keeps ids unique across restarts; the counter keeps them
  // unique within a process.
  return "evt-" + std::to_string(NowMs()) + "-" + std::to_string(++event_id_counter_);
}

}  // namespace chirp::server_gateway
