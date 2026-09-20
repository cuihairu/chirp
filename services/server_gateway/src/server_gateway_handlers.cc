#include "server_gateway_handlers.h"

#include <chrono>
#include <utility>
#include <vector>

#include "logger.h"

namespace chirp::server_gateway {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

ServerGatewayHandlers::ServerGatewayHandlers(ServerGatewayConfig config, ServiceRegistry& registry,
                                             EventQueue& queue, IdentityRegistry& identities,
                                             SubscriptionRegistry& subscriptions)
    : config_(std::move(config)),
      registry_(registry),
      queue_(queue),
      identities_(identities),
      subscriptions_(subscriptions) {}

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
// GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
}

ServerHeartbeatPong ServerGatewayHandlers::HandleHeartbeat(const ServerHeartbeatPing& /*ping*/) const {
  ServerHeartbeatPong pong;
  pong.set_server_time_ms(NowMs());
  return pong;
// GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
}

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
    chirp::common::Logger::Instance().Warn(
        "inject " + req.inject_id() + " from=" + req.sender_id() + " to=" +
        req.receiver_id() + ": chat service unavailable");
    resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    return resp;
  }
  chirp::common::Logger::Instance().Info(
      "inject " + req.inject_id() + " from=" + req.sender_id() + " to=" +
      req.receiver_id() + " delivered to " + config_.chat_service_id);
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
  chirp::common::Logger::Instance().Info(
      "event " + event.event_id + " type=" + event.event_type + " target=" +
      req.target_service_id() +
      (target ? " delivered" : " queued (target offline)"));
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
// GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
}

BindPlayerIdentityResponse ServerGatewayHandlers::HandleBindPlayerIdentity(
    const BindPlayerIdentityRequest& req) {
  BindPlayerIdentityResponse resp;
  resp.set_binding_id(req.binding_id());
  const auto outcome =
      identities_.Bind(req.binding_id(), req.player_id(), req.game_id(), req.game_user_id(),
                       /*bound_at_ms=*/NowMs());
  switch (outcome) {
  case IdentityRegistry::BindOutcome::kBound:
    chirp::common::Logger::Instance().Info(
        "binding " + req.binding_id() + ": player " + req.player_id() + " <- " + req.game_id() +
        ":" + req.game_user_id());
    resp.set_code(chirp::common::OK);
    break;
  case IdentityRegistry::BindOutcome::kExisted:
    resp.set_code(chirp::common::OK);
    resp.set_existed(true);
    break;
  case IdentityRegistry::BindOutcome::kInvalid:
    resp.set_code(chirp::common::INVALID_PARAM);
    break;
  }
  return resp;
}

UnbindPlayerIdentityResponse ServerGatewayHandlers::HandleUnbindPlayerIdentity(
    const UnbindPlayerIdentityRequest& req) {
  UnbindPlayerIdentityResponse resp;
  // Exactly one selector: binding_id, or the complete (game_id,
  // game_user_id) pair — anything else is a bad request, not a no-op.
  const bool by_id = !req.binding_id().empty();
  const bool by_pair = !req.game_id().empty() && !req.game_user_id().empty();
  const bool malformed_pair = req.game_id().empty() != req.game_user_id().empty();
  if (by_id == by_pair || malformed_pair) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  const bool removed =
      by_id ? identities_.UnbindById(req.binding_id())
            : identities_.UnbindByGameUser(req.game_id(), req.game_user_id());
  resp.set_code(chirp::common::OK);
  if (removed) {
    chirp::common::Logger::Instance().Info(
        "unbound " + (by_id ? req.binding_id() : req.game_id() + ":" + req.game_user_id()));
  }
  return resp;
}

GetPlayerIdentitiesResponse ServerGatewayHandlers::HandleGetPlayerIdentities(
    const GetPlayerIdentitiesRequest& req) const {
  GetPlayerIdentitiesResponse resp;
  if (req.player_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  for (const auto& entry : identities_.GetByPlayer(req.player_id())) {
    *resp.add_bindings() = entry;
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

ResolveGameUserResponse ServerGatewayHandlers::HandleResolveGameUser(
    const ResolveGameUserRequest& req) const {
  ResolveGameUserResponse resp;
  if (req.game_id().empty() || req.game_user_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  const auto player = identities_.Resolve(req.game_id(), req.game_user_id());
  if (player) {
    resp.set_player_id(*player);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

SubscribePlayerChannelResponse ServerGatewayHandlers::HandleSubscribePlayerChannel(
    const SubscribePlayerChannelRequest& req) {
  SubscribePlayerChannelResponse resp;
  // The registry mints an id when the caller supplied none (the app edge's
  // self-service path), so only the tuple fields must be present here.
  if (req.player_id().empty() || req.game_id().empty() || req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  std::string subscription_id = req.subscription_id();
  const auto outcome =
      subscriptions_.Subscribe(&subscription_id, req.player_id(), req.game_id(), req.channel_id(),
                               /*subscribed_at_ms=*/NowMs());
  switch (outcome) {
  case SubscriptionRegistry::SubscribeOutcome::kSubscribed:
    chirp::common::Logger::Instance().Info(
        "subscribed " + subscription_id + ": player " + req.player_id() + " -> " + req.game_id() +
        ":" + req.channel_id());
    resp.set_code(chirp::common::OK);
    break;
  case SubscriptionRegistry::SubscribeOutcome::kExisted:
    resp.set_code(chirp::common::OK);
    resp.set_existed(true);
    break;
  case SubscriptionRegistry::SubscribeOutcome::kInvalid:
    resp.set_code(chirp::common::INVALID_PARAM);
    break;
  }
  resp.set_subscription_id(subscription_id);
  return resp;
}

UnsubscribePlayerChannelResponse ServerGatewayHandlers::HandleUnsubscribePlayerChannel(
    const UnsubscribePlayerChannelRequest& req) {
  UnsubscribePlayerChannelResponse resp;
  // Exactly one selector: subscription_id, or the complete (player_id,
  // game_id, channel_id) triple — anything else is a bad request, not a
  // no-op.
  const bool by_id = !req.subscription_id().empty();
  const bool by_tuple =
      !req.player_id().empty() && !req.game_id().empty() && !req.channel_id().empty();
  const bool malformed_tuple =
      !(req.player_id().empty() && req.game_id().empty() && req.channel_id().empty()) && !by_tuple;
  if (by_id == by_tuple || malformed_tuple) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  const bool removed =
      by_id ? subscriptions_.UnsubscribeById(req.subscription_id())
            : subscriptions_.UnsubscribeByTuple(req.player_id(), req.game_id(), req.channel_id());
  resp.set_code(chirp::common::OK);
  if (removed) {
    chirp::common::Logger::Instance().Info(
        "unsubscribed " +
        (by_id ? req.subscription_id()
               : req.player_id() + ":" + req.game_id() + ":" + req.channel_id()));
  }
  return resp;
}

GetPlayerSubscriptionsResponse ServerGatewayHandlers::HandleGetPlayerSubscriptions(
    const GetPlayerSubscriptionsRequest& req) const {
  GetPlayerSubscriptionsResponse resp;
  if (req.player_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  for (const auto& entry : subscriptions_.GetForPlayer(req.player_id(), req.game_id())) {
    *resp.add_subscriptions() = entry;
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

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
