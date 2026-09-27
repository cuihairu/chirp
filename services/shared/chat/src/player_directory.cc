#include "player_directory.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "logger.h"
#include "runtime_utils.h"

namespace chirp::chat {
namespace {
// Derived roster: which games a player's presence currently covers. One key
// per player, newline-joined sorted game ids (DEL when empty). Read by the
// social plane's presence queries; ownership is this directory's.
constexpr const char* kOnlinePrefix = "chirp:game_presence:online:";
// Pub/sub channel: one serialized GamePresenceEvent per game flip, consumed
// by the social plane to push IN_GAME / back-to-online to friends.
constexpr const char* kEventsChannel = "chirp:game_presence:events";
}  // namespace

PlayerDirectory::PlayerDirectory(Options options)
    : options_(std::move(options)),
      identities_(options_.identities_redis ? std::move(options_.identities_redis)
                                            : IdentityRegistry::RedisFactory()),
      subscriptions_(options_.subscriptions_redis ? std::move(options_.subscriptions_redis)
                                                  : SubscriptionRegistry::RedisFactory()),
      unread_(options_.unread_redis ? std::move(options_.unread_redis)
                                    : UnreadLedger::RedisFactory()),
      // Copied, not moved: RefreshPresence needs the factory too (it opens a
      // short-lived client per roster refresh), unlike the registries above
      // which each keep a single long-lived client.
      presence_(options_.presence_redis) {}

void PlayerDirectory::LoadAll() {
  identities_.Load();
  subscriptions_.Load();
  unread_.Load();
  presence_.Load();
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
    // A new binding is a new presence assertion: the derived roster and the
    // friend-facing status follow it (when the switch is on, which is the
    // default).
    RefreshPresence(req.player_id());
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
  // The unbind results carry no owner, but the presence refresh needs the
  // affected player — resolve them before the row disappears.
  std::string affected_player;
  if (by_id) {
    const auto entry = identities_.GetById(req.binding_id());
    if (entry) {
      affected_player = entry->player_id();
    }
  } else if (const auto player = identities_.Resolve(req.game_id(), req.game_user_id())) {
    affected_player = *player;
  }
  const bool removed =
      by_id ? identities_.UnbindById(req.binding_id())
            : identities_.UnbindByGameUser(req.game_id(), req.game_user_id());
  resp.set_code(chirp::common::OK);
  if (removed) {
    chirp::common::Logger::Instance().Info(
        "unbound " + (by_id ? req.binding_id() : req.game_id() + ":" + req.game_user_id()));
    // Losing the last binding is what takes a player's game status offline
    // naturally — the assertion is the binding, not a liveness signal.
    if (!affected_player.empty()) {
      RefreshPresence(affected_player);
    }
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

game_server_gateway::SetGamePresenceEnabledResponse
PlayerDirectory::HandleSetGamePresenceEnabled(
    const game_server_gateway::SetGamePresenceEnabledRequest& req) {
  game_server_gateway::SetGamePresenceEnabledResponse resp;
  if (req.player_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (presence_.SetEnabled(req.player_id(), req.enabled())) {
    // The switch flip gates both the friend-facing roster and the DM relay
    // below, so the derived state follows it immediately (disabling drops
    // the roster and takes the status offline right away).
    RefreshPresence(req.player_id());
    chirp::common::Logger::Instance().Info(
        "game presence for player " + req.player_id() + " -> " +
        (req.enabled() ? "enabled" : "disabled"));
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

game_server_gateway::GetGamePresenceResponse PlayerDirectory::HandleGetGamePresence(
    const game_server_gateway::GetGamePresenceRequest& req) const {
  game_server_gateway::GetGamePresenceResponse resp;
  if (req.player_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  resp.set_code(chirp::common::OK);
  resp.set_enabled(presence_.Enabled(req.player_id()));
  if (!resp.enabled()) {
    return resp;
  }
  // Entries are the live bindings sorted by game_id — the games the
  // presence currently covers, straight from the assertion store.
  auto bindings = identities_.GetByPlayer(req.player_id());
  std::sort(bindings.begin(), bindings.end(),
            [](const auto& a, const auto& b) { return a.game_id() < b.game_id(); });
  for (const auto& binding : bindings) {
    auto* entry = resp.add_entries();
    entry->set_game_id(binding.game_id());
    entry->set_game_user_id(binding.game_user_id());
  }
  return resp;
}

void PlayerDirectory::RefreshPresence(const std::string& player_id) {
  if (player_id.empty()) {
    return;
  }
  // Desired state: the sorted game ids of the live bindings, but only while
  // the switch is on. Disabled players keep their bindings yet drop out of
  // the roster entirely — that is the closed-state contract (状态不推).
  std::set<std::string> desired;
  if (presence_.Enabled(player_id)) {
    for (const auto& binding : identities_.GetByPlayer(player_id)) {
      desired.insert(binding.game_id());
    }
  }

  auto& published = published_games_[player_id];
  if (published == desired) {
    return;
  }

  // One shared connection serves the roster write and every event publish.
  auto redis = options_.presence_redis ? options_.presence_redis() : nullptr;
  auto publish = [&redis](const game_server_gateway::GamePresenceEvent& event) {
    if (!redis || redis->Publish(kEventsChannel, event.SerializeAsString())) {
      return;
    }
    chirp::common::Logger::Instance().Warn(
        "game presence: failed to publish event for player " + event.player_id());
  };
  for (const auto& game_id : published) {
    if (desired.count(game_id) == 0) {
      game_server_gateway::GamePresenceEvent event;
      event.set_player_id(player_id);
      event.set_game_id(game_id);
      event.set_online(false);
      publish(event);
    }
  }
  for (const auto& game_id : desired) {
    if (published.count(game_id) == 0) {
      game_server_gateway::GamePresenceEvent event;
      event.set_player_id(player_id);
      event.set_game_id(game_id);
      event.set_online(true);
      publish(event);
      chirp::common::Logger::Instance().Info(
          "game presence: player " + player_id + " entered game " + game_id);
    }
  }
  published = desired;

  if (redis) {
    const std::string key = std::string(kOnlinePrefix) + player_id;
    if (desired.empty()) {
      redis->Del(key);
    } else {
      std::string joined;
      for (const auto& game_id : desired) {
        if (!joined.empty()) {
          joined.push_back('\n');
        }
        joined += game_id;
      }
      redis->Set(key, joined);
    }
  }
}

size_t PlayerDirectory::RelayFriendMessage(const std::string& sender_player_id,
                                           const std::string& recipient_player_id,
                                           const std::string& content,
                                           const std::string& client_msg_id,
                                           const GameServiceResolver& resolve,
                                           const GameReplySender& inject) const {
  if (sender_player_id.empty() || recipient_player_id.empty()) {
    return 0;
  }
  // The switch is the off switch for exactly this relay and the status
  // push: a disabled player gets neither (关闭态回归语义, tested).
  if (!presence_.Enabled(recipient_player_id)) {
    return 0;
  }
  auto bindings = identities_.GetByPlayer(recipient_player_id);
  if (bindings.empty()) {
    return 0;
  }
  // Deterministic order: one binding per game, sorted by game_id.
  std::sort(bindings.begin(), bindings.end(),
            [](const auto& a, const auto& b) { return a.game_id() < b.game_id(); });

  size_t injected = 0;
  for (const auto& binding : bindings) {
    const std::string service_id = resolve(binding.game_id());
    if (service_id.empty()) {
      // No live spoke for this game: a logged skip, never an error — the
      // ordinary chat delivery above already succeeded.
      continue;
    }
    gateway::PeerInjectMessageNotify notify;
    // The spoke treats channel_id as the target user: the private copy is
    // addressed to the bound game identity, sender is the chirp friend.
    notify.set_channel_id(binding.game_user_id());
    notify.set_sender_id(sender_player_id);
    notify.set_content(content);
    notify.set_client_msg_id(client_msg_id);
    if (!inject(service_id, notify)) {
      continue;
    }
    ++injected;
    chirp::common::Logger::Instance().Info(
        "friend DM relayed into game " + binding.game_id() + " for " + recipient_player_id +
        " (" + binding.game_user_id() + ") from " + sender_player_id);
  }
  return injected;
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

PlayerDirectory::GameReplyOutcome PlayerDirectory::RelayGameReply(
    const std::string& sender_player_id, const std::string& channel_id,
    const std::string& content, const std::string& client_msg_id,
    const GameServiceResolver& resolve, const GameReplySender& inject) const {
  // Game ids cannot contain ':' (the registries reject them at bind time),
  // so the first colon separates a game prefix from the bare game-side
  // channel. Anything else is an ordinary App-side send.
  const size_t sep = channel_id.find(':');
  if (sep == std::string::npos || sep == 0 || sep + 1 >= channel_id.size()) {
    return GameReplyOutcome::kNoGamePrefix;
  }
  const std::string game_id = channel_id.substr(0, sep);
  const std::string bare = channel_id.substr(sep + 1);

  // No live spoke for the game: refuse instead of falling back to a local
  // "<game_id>:<bare>" channel, so the client never mistakes an undelivered
  // reply for a delivered one.
  const std::string service_id = resolve(game_id);
  if (service_id.empty()) {
    return GameReplyOutcome::kUnknownGame;
  }

  const auto game_user = identities_.ResolveGameUser(game_id, sender_player_id);
  if (!game_user) {
    return GameReplyOutcome::kUnboundPlayer;
  }

  gateway::PeerInjectMessageNotify notify;
  notify.set_channel_id(bare);
  notify.set_sender_id(*game_user);
  notify.set_content(content);
  notify.set_client_msg_id(client_msg_id);
  if (!inject(service_id, notify)) {
    return GameReplyOutcome::kSendFailed;
  }
  chirp::common::Logger::Instance().Info(
      "cross-plane reply: player=" + sender_player_id + " game_user=" + *game_user + " game=" +
      game_id + " channel=" + bare + " spoke=" + service_id);
  return GameReplyOutcome::kSent;
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
  const bool in_block = id >= MsgID::BIND_PLAYER_IDENTITY_REQ && id <= MsgID::GET_GAME_PRESENCE_RESP;
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
  case MsgID::SET_GAME_PRESENCE_ENABLED_REQ:
    ServeRpc<game_server_gateway::SetGamePresenceEnabledRequest,
             game_server_gateway::SetGamePresenceEnabledResponse>(
        session, pkt, MsgID::SET_GAME_PRESENCE_ENABLED_RESP, trusted,
        [&directory](const game_server_gateway::SetGamePresenceEnabledRequest& req) {
          return directory.HandleSetGamePresenceEnabled(req);
        });
    return true;
  case MsgID::GET_GAME_PRESENCE_REQ:
    ServeRpc<game_server_gateway::GetGamePresenceRequest,
             game_server_gateway::GetGamePresenceResponse>(
        session, pkt, MsgID::GET_GAME_PRESENCE_RESP, trusted,
        [&directory](const game_server_gateway::GetGamePresenceRequest& req) {
          return directory.HandleGetGamePresence(req);
        });
    return true;
  default:
    chirp::common::Logger::Instance().Warn(
        "player directory: dropping unexpected response id " + std::to_string(static_cast<int>(id)));
    return true;
  }
}

}  // namespace chirp::chat
