#include "player_directory.h"

#include <functional>
#include <utility>

#include "logger.h"
#include "runtime_utils.h"

namespace chirp::chat {

PlayerDirectory::PlayerDirectory(Options options)
    : options_(std::move(options)),
      identities_(options_.identities_redis ? std::move(options_.identities_redis)
                                            : IdentityRegistry::RedisFactory()),
      subscriptions_(options_.subscriptions_redis ? std::move(options_.subscriptions_redis)
                                                  : SubscriptionRegistry::RedisFactory()),
      unread_(options_.unread_redis ? std::move(options_.unread_redis)
                                    : UnreadLedger::RedisFactory()) {}

void PlayerDirectory::LoadAll() {
  identities_.Load();
  subscriptions_.Load();
  unread_.Load();
}

game_server_gateway::BindPlayerIdentityResponse PlayerDirectory::HandleBindPlayerIdentity(
    const game_server_gateway::BindPlayerIdentityRequest& req) {
  game_server_gateway::BindPlayerIdentityResponse resp;
  resp.set_binding_id(req.binding_id());
  const auto outcome =
      identities_.Bind(req.binding_id(), req.player_id(), req.game_id(), req.game_user_id(),
                       /*bound_at_ms=*/runtime::NowMs());
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

game_server_gateway::UnbindPlayerIdentityResponse PlayerDirectory::HandleUnbindPlayerIdentity(
    const game_server_gateway::UnbindPlayerIdentityRequest& req) {
  game_server_gateway::UnbindPlayerIdentityResponse resp;
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

game_server_gateway::GetPlayerIdentitiesResponse PlayerDirectory::HandleGetPlayerIdentities(
    const game_server_gateway::GetPlayerIdentitiesRequest& req) const {
  game_server_gateway::GetPlayerIdentitiesResponse resp;
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

game_server_gateway::ResolveGameUserResponse PlayerDirectory::HandleResolveGameUser(
    const game_server_gateway::ResolveGameUserRequest& req) const {
  game_server_gateway::ResolveGameUserResponse resp;
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

game_server_gateway::SubscribePlayerChannelResponse PlayerDirectory::HandleSubscribePlayerChannel(
    const game_server_gateway::SubscribePlayerChannelRequest& req) {
  game_server_gateway::SubscribePlayerChannelResponse resp;
  // The registry mints an id when the caller supplied none (the app edge's
  // self-service path), so only the tuple fields must be present here.
  if (req.player_id().empty() || req.game_id().empty() || req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  std::string subscription_id = req.subscription_id();
  const auto outcome =
      subscriptions_.Subscribe(&subscription_id, req.player_id(), req.game_id(), req.channel_id(),
                               /*subscribed_at_ms=*/runtime::NowMs());
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

game_server_gateway::UnsubscribePlayerChannelResponse
PlayerDirectory::HandleUnsubscribePlayerChannel(
    const game_server_gateway::UnsubscribePlayerChannelRequest& req) {
  game_server_gateway::UnsubscribePlayerChannelResponse resp;
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

game_server_gateway::GetPlayerSubscriptionsResponse PlayerDirectory::HandleGetPlayerSubscriptions(
    const game_server_gateway::GetPlayerSubscriptionsRequest& req) const {
  game_server_gateway::GetPlayerSubscriptionsResponse resp;
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

game_server_gateway::MarkChannelsReadResponse PlayerDirectory::HandleMarkChannelsRead(
    const game_server_gateway::MarkChannelsReadRequest& req) {
  game_server_gateway::MarkChannelsReadResponse resp;
  // Layered selector: channel_id needs game_id; game_id alone or both empty
  // are valid (clear the game / clear everything). The app edge pins
  // player_id, but backend callers go through this path directly too.
  if (req.player_id().empty() || (!req.channel_id().empty() && req.game_id().empty())) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  const size_t cleared = unread_.MarkRead(req.player_id(), req.game_id(), req.channel_id());
  resp.set_code(chirp::common::OK);
  resp.set_cleared(static_cast<int32_t>(cleared));
  if (cleared > 0) {
    chirp::common::Logger::Instance().Info(
        "marked read for player " + req.player_id() + ": cleared " + std::to_string(cleared) +
        " unread entries");
  }
  return resp;
}

game_server_gateway::GetUnreadSummaryResponse PlayerDirectory::HandleGetUnreadSummary(
    const game_server_gateway::GetUnreadSummaryRequest& req) const {
  game_server_gateway::GetUnreadSummaryResponse resp;
  if (req.player_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  int32_t total = 0;
  for (const auto& entry : unread_.GetSummary(req.player_id(), req.game_id())) {
    total += entry.unread_count();
    *resp.add_entries() = entry;
  }
  resp.set_code(chirp::common::OK);
  resp.set_total_unread(total);
  return resp;
}

size_t PlayerDirectory::FanoutChannelMessage(const gateway::ChannelMessageNotify& notify) {
  // Snapshot under the registry lock; the delivery below must not hold it.
  const auto subscribers = subscriptions_.GetForChannel(notify.game_id(), notify.channel_id());
  if (subscribers.empty()) {
    // Nobody follows the channel: a semantic no-op, the uplink is done.
    return 0;
  }
  if (subscribers.size() > options_.max_fanout_per_message) {
    chirp::common::Logger::Instance().Warn(
        "fan-out to " + std::to_string(subscribers.size()) + " subscribers of " +
        notify.game_id() + ":" + notify.channel_id() +
        " exceeds --max_fanout_per_message, dropped");
    return 0;
  }

  size_t delivered = 0;
  for (const auto& sub : subscribers) {
    if (options_.deliver_copy) {
      options_.deliver_copy(sub.player_id(), notify);
    }
    // A copy accepted by the plane is one unhandled notification for the
    // recipient (WP-8 slice 4); the delivery hook has no failure path —
    // offline recipients land in the queue, which still counts as pending.
    unread_.Increment(sub.player_id(), notify.game_id(), notify.channel_id());
    ++delivered;
  }
  chirp::common::Logger::Instance().Info(
      "fanned out to " + std::to_string(delivered) + " subscribers of " + notify.game_id() + ":" +
      notify.channel_id());
  return delivered;
}

namespace {

// The callers of the WP-8 block are backend services over the trusted
// internal plane; an untrusted dial gets a loud refusal per response type.
template <typename Resp>
void DenyUntrusted(const std::shared_ptr<network::Session>& session, const gateway::Packet& pkt,
                   gateway::MsgID resp_id) {
  Resp resp;
  resp.set_code(chirp::common::AUTH_FAILED);
  runtime::SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());
}

template <typename Req>
bool ParseBody(const gateway::Packet& pkt, Req* req) {
  return req->ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
}

template <typename Resp>
void SendResp(const std::shared_ptr<network::Session>& session, const gateway::Packet& pkt,
              gateway::MsgID resp_id, const Resp& resp) {
  runtime::SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());
}

// One WP-8 request end to end: trust gate, parse gate, handler, response.
template <typename Req, typename Resp>
void ServeRpc(const std::shared_ptr<network::Session>& session, const gateway::Packet& pkt,
              gateway::MsgID resp_id, bool trusted, const std::function<Resp(const Req&)>& serve) {
  if (!trusted) {
    DenyUntrusted<Resp>(session, pkt, resp_id);
    return;
  }
  Req req;
  if (!ParseBody(pkt, &req)) {
    Resp resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    SendResp(session, pkt, resp_id, resp);
    return;
  }
  SendResp(session, pkt, resp_id, serve(req));
}

}  // namespace

bool DispatchPlayerDirectoryPacket(
    const gateway::Packet& pkt, const std::shared_ptr<network::Session>& session,
    PlayerDirectory& directory,
    const std::unordered_set<const network::Session*>* trusted_conns) {
  using chirp::gateway::MsgID;
  const MsgID id = pkt.msg_id();
  const bool in_block = id >= MsgID::BIND_PLAYER_IDENTITY_REQ && id <= MsgID::GET_UNREAD_SUMMARY_RESP;
  if (!in_block) {
    return false;
  }
  // Odd ids in the block are requests; an even id (a response) arriving
  // from a peer is bogus — consumed with a warning, never fed to a handler
  // and never answered (there is no response id to echo a denial on that
  // would make sense to the sender).
  const bool trusted = trusted_conns && trusted_conns->count(session.get()) > 0;
  switch (id) {
  case MsgID::BIND_PLAYER_IDENTITY_REQ:
    ServeRpc<game_server_gateway::BindPlayerIdentityRequest,
             game_server_gateway::BindPlayerIdentityResponse>(
        session, pkt, MsgID::BIND_PLAYER_IDENTITY_RESP, trusted,
        [&directory](const game_server_gateway::BindPlayerIdentityRequest& req) {
          return directory.HandleBindPlayerIdentity(req);
        });
    return true;
  case MsgID::UNBIND_PLAYER_IDENTITY_REQ:
    ServeRpc<game_server_gateway::UnbindPlayerIdentityRequest,
             game_server_gateway::UnbindPlayerIdentityResponse>(
        session, pkt, MsgID::UNBIND_PLAYER_IDENTITY_RESP, trusted,
        [&directory](const game_server_gateway::UnbindPlayerIdentityRequest& req) {
          return directory.HandleUnbindPlayerIdentity(req);
        });
    return true;
  case MsgID::GET_PLAYER_IDENTITIES_REQ:
    ServeRpc<game_server_gateway::GetPlayerIdentitiesRequest,
             game_server_gateway::GetPlayerIdentitiesResponse>(
        session, pkt, MsgID::GET_PLAYER_IDENTITIES_RESP, trusted,
        [&directory](const game_server_gateway::GetPlayerIdentitiesRequest& req) {
          return directory.HandleGetPlayerIdentities(req);
        });
    return true;
  case MsgID::RESOLVE_GAME_USER_REQ:
    ServeRpc<game_server_gateway::ResolveGameUserRequest,
             game_server_gateway::ResolveGameUserResponse>(
        session, pkt, MsgID::RESOLVE_GAME_USER_RESP, trusted,
        [&directory](const game_server_gateway::ResolveGameUserRequest& req) {
          return directory.HandleResolveGameUser(req);
        });
    return true;
  case MsgID::SUBSCRIBE_PLAYER_CHANNEL_REQ:
    ServeRpc<game_server_gateway::SubscribePlayerChannelRequest,
             game_server_gateway::SubscribePlayerChannelResponse>(
        session, pkt, MsgID::SUBSCRIBE_PLAYER_CHANNEL_RESP, trusted,
        [&directory](const game_server_gateway::SubscribePlayerChannelRequest& req) {
          return directory.HandleSubscribePlayerChannel(req);
        });
    return true;
  case MsgID::UNSUBSCRIBE_PLAYER_CHANNEL_REQ:
    ServeRpc<game_server_gateway::UnsubscribePlayerChannelRequest,
             game_server_gateway::UnsubscribePlayerChannelResponse>(
        session, pkt, MsgID::UNSUBSCRIBE_PLAYER_CHANNEL_RESP, trusted,
        [&directory](const game_server_gateway::UnsubscribePlayerChannelRequest& req) {
          return directory.HandleUnsubscribePlayerChannel(req);
        });
    return true;
  case MsgID::GET_PLAYER_SUBSCRIPTIONS_REQ:
    ServeRpc<game_server_gateway::GetPlayerSubscriptionsRequest,
             game_server_gateway::GetPlayerSubscriptionsResponse>(
        session, pkt, MsgID::GET_PLAYER_SUBSCRIPTIONS_RESP, trusted,
        [&directory](const game_server_gateway::GetPlayerSubscriptionsRequest& req) {
          return directory.HandleGetPlayerSubscriptions(req);
        });
    return true;
  case MsgID::MARK_CHANNELS_READ_REQ:
    ServeRpc<game_server_gateway::MarkChannelsReadRequest,
             game_server_gateway::MarkChannelsReadResponse>(
        session, pkt, MsgID::MARK_CHANNELS_READ_RESP, trusted,
        [&directory](const game_server_gateway::MarkChannelsReadRequest& req) {
          return directory.HandleMarkChannelsRead(req);
        });
    return true;
  case MsgID::GET_UNREAD_SUMMARY_REQ:
    ServeRpc<game_server_gateway::GetUnreadSummaryRequest,
             game_server_gateway::GetUnreadSummaryResponse>(
        session, pkt, MsgID::GET_UNREAD_SUMMARY_RESP, trusted,
        [&directory](const game_server_gateway::GetUnreadSummaryRequest& req) {
          return directory.HandleGetUnreadSummary(req);
        });
    return true;
  default:
    chirp::common::Logger::Instance().Warn(
        "player directory: dropping unexpected response id " + std::to_string(static_cast<int>(id)));
    return true;
  }
}

}  // namespace chirp::chat
