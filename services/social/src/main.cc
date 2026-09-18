// NOTE: never include presence_manager.h from this TU. It declares its own
// chirp::social::PresenceStatus enum that collides with the proto-generated
// one used below.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "common/login_token_verifier.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/social.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  return static_cast<uint16_t>(std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str()));
}

std::string RandomHex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  static const char* kHex = "0123456789abcdef";

  std::string out;
  out.resize(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    uint8_t b = static_cast<uint8_t>(dist(rng));
    out[i * 2] = kHex[(b >> 4) & 0xF];
    out[i * 2 + 1] = kHex[b & 0xF];
  }
  return out;
}

// Redis snapshot keys, one per user. The values are the Stored* messages from
// proto/social.proto (storage schema, not wire protocol).
constexpr const char* kFriendsPrefix = "chirp:social:friends:";
constexpr const char* kPendingPrefix = "chirp:social:pending:";
constexpr const char* kBlockedPrefix = "chirp:social:blocked:";

// Social service state. Lock discipline: never call a SessionRegistry free
// function while holding `mu` - the registry has its own lock and the two are
// always taken strictly one after the other (state mutation first, then
// registry lookups for delivery outside the lock).
struct SocialState {
  std::mutex mu;

  // Friend relationships: user_id -> set of friend_ids
  std::unordered_map<std::string, std::unordered_set<std::string>> friends;

  // Pending friend requests: request_id -> request. Both directions of a
  // conversation are kept (the to_user side is the incoming request, the
  // from_user side the outgoing record); GET_PENDING_REQUESTS only returns
  // the incoming side.
  std::unordered_map<std::string, chirp::social::FriendRequest> pending_requests;

  // Blocked users: user_id -> set of blocked_user_ids
  std::unordered_map<std::string, std::unordered_set<std::string>> blocked;

  // Presence: user_id -> presence info
  std::unordered_map<std::string, chirp::social::PresenceInfo> presence;

  // Authenticated (user, device) -> session bindings; the single source of
  // truth for who is connected and where to deliver.
  std::shared_ptr<chirp::network::SessionRegistry> registry =
      std::make_shared<chirp::network::SessionRegistry>();
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void SendPacketAndClose(const std::shared_ptr<chirp::network::Session>& session,
                        chirp::gateway::MsgID msg_id,
                        int64_t seq,
                        const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void KickSession(const std::shared_ptr<chirp::network::Session>& session, const std::string& reason) {
  chirp::auth::KickNotify kick;
  kick.set_reason(reason.empty() ? "kicked" : reason);
  SendPacketAndClose(session, chirp::gateway::KICK_NOTIFY, 0, kick.SerializeAsString());
}

// Delivers a packet to every live session of user_id (all devices). Registry
// lookups happen outside the state lock; the send itself is fire-and-forget.
void SendToUser(const std::shared_ptr<SocialState>& state,
                const std::string& user_id,
                chirp::gateway::MsgID msg_id,
                const std::string& body) {
  for (const auto& sess : chirp::network::GetUserSessions(state->registry, user_id)) {
    SendPacket(sess, msg_id, 0, body);
  }
}

void BroadcastToFriends(const std::shared_ptr<SocialState>& state,
                        const std::string& user_id,
                        chirp::gateway::MsgID msg_id,
                        const std::string& body) {
  std::vector<std::string> friend_ids;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->friends.find(user_id);
    if (it != state->friends.end()) {
      friend_ids.assign(it->second.begin(), it->second.end());
    }
  }

  for (const auto& friend_id : friend_ids) {
    SendToUser(state, friend_id, msg_id, body);
  }
}

// ---------------------------------------------------------------------------
// Redis persistence. Best-effort write-through: the in-memory state is the
// working authority, Redis only restores it after a restart, so a failed
// write costs durability, never the in-flight request.
// ---------------------------------------------------------------------------

void PersistFriends(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_set<std::string>& friend_ids) {
  if (!redis) {
    return;
  }
  chirp::social::StoredFriendList stored;
  for (const auto& id : friend_ids) {
    stored.add_friend_user_ids(id);
  }
  if (!redis->Set(std::string(kFriendsPrefix) + user_id, stored.SerializeAsString())) {
    chirp::common::Logger::Instance().Warn("social: failed to persist friends of " + user_id);
  }
}

void PersistPending(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_map<std::string, chirp::social::FriendRequest>& pending) {
  if (!redis) {
    return;
  }
  chirp::social::StoredPendingRequests stored;
  for (const auto& [request_id, req] : pending) {
    // Both directions live in the shared table; each user's snapshot carries
    // the requests they are a party to (incoming via to_user_id, outgoing
    // via from_user_id).
    if (req.to_user_id() != user_id && req.from_user_id() != user_id) {
      continue;
    }
    (*stored.mutable_requests())[request_id] = req;
  }
  if (!redis->Set(std::string(kPendingPrefix) + user_id, stored.SerializeAsString())) {
    chirp::common::Logger::Instance().Warn("social: failed to persist pending requests of " + user_id);
  }
}

void PersistBlocked(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_set<std::string>& blocked_ids) {
  if (!redis) {
    return;
  }
  chirp::social::StoredBlockedList stored;
  for (const auto& id : blocked_ids) {
    stored.add_blocked_user_ids(id);
  }
  if (!redis->Set(std::string(kBlockedPrefix) + user_id, stored.SerializeAsString())) {
    chirp::common::Logger::Instance().Warn("social: failed to persist blocked list of " + user_id);
  }
}

// Restores friends/pending/blocked from Redis snapshots. Returns false when
// Redis is unreachable (pure in-memory mode); a single corrupt key is skipped
// with a warning rather than failing the load.
bool LoadSocialState(const std::shared_ptr<chirp::network::RedisClient>& redis,
                     const std::shared_ptr<SocialState>& state) {
  using chirp::common::Logger;
  if (!redis) {
    return false;
  }
  if (!redis->Command({"PING"})) {
    Logger::Instance().Warn("social: redis unreachable, starting with empty in-memory state");
    return false;
  }

  for (const auto& key : redis->Keys(std::string(kFriendsPrefix) + "*")) {
    const std::string user_id = key.substr(std::string_view(kFriendsPrefix).size());
    const auto value = redis->Get(key);
    chirp::social::StoredFriendList stored;
    if (user_id.empty() || !value || !stored.ParseFromString(*value)) {
      Logger::Instance().Warn("social: skipping corrupt friend snapshot " + key);
      continue;
    }
    std::lock_guard<std::mutex> lock(state->mu);
    auto& ids = state->friends[user_id];
    for (const auto& id : stored.friend_user_ids()) {
      ids.insert(id);
    }
  }

  for (const auto& key : redis->Keys(std::string(kPendingPrefix) + "*")) {
    const auto value = redis->Get(key);
    chirp::social::StoredPendingRequests stored;
    if (!value || !stored.ParseFromString(*value)) {
      Logger::Instance().Warn("social: skipping corrupt pending snapshot " + key);
      continue;
    }
    std::lock_guard<std::mutex> lock(state->mu);
    for (const auto& [request_id, req] : stored.requests()) {
      // Both parties persist the same request; the first snapshot wins and
      // the duplicate is dropped.
      if (state->pending_requests.find(request_id) == state->pending_requests.end()) {
        state->pending_requests[request_id] = req;
      }
    }
  }

  for (const auto& key : redis->Keys(std::string(kBlockedPrefix) + "*")) {
    const std::string user_id = key.substr(std::string_view(kBlockedPrefix).size());
    const auto value = redis->Get(key);
    chirp::social::StoredBlockedList stored;
    if (user_id.empty() || !value || !stored.ParseFromString(*value)) {
      Logger::Instance().Warn("social: skipping corrupt blocked snapshot " + key);
      continue;
    }
    std::lock_guard<std::mutex> lock(state->mu);
    auto& ids = state->blocked[user_id];
    for (const auto& id : stored.blocked_user_ids()) {
      ids.insert(id);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Shared response/notify builders
// ---------------------------------------------------------------------------

chirp::social::PresenceNotify MakePresenceNotify(const std::string& user_id,
                                                 chirp::social::PresenceStatus status,
                                                 const std::string& status_message) {
  chirp::social::PresenceNotify notify;
  notify.set_user_id(user_id);
  notify.set_status(status);
  notify.set_status_message(status_message);
  notify.set_timestamp(NowMs());
  return notify;
}

// Guards a business handler: the caller must be a logged-in session, and a
// non-empty req.user_id must match the authenticated identity. Returns the
// authenticated user id, or empty after sending an error response.
template <typename RespT>
std::string RequireUser(const std::shared_ptr<SocialState>& state,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt,
                        chirp::gateway::MsgID resp_id,
                        const std::string& req_user_id,
                        RespT* resp) {
  const auto authenticated = chirp::network::GetAuthenticatedSession(state->registry, session);
  // Not every response message carries server_time (the GET_* ones don't).
  const auto stamp = [&] {
    if constexpr (requires { resp->set_server_time(NowMs()); }) {
      resp->set_server_time(NowMs());
    }
  };
  if (authenticated.user_id.empty()) {
    resp->set_code(chirp::common::AUTH_FAILED);
    stamp();
    SendPacket(session, resp_id, pkt.sequence(), resp->SerializeAsString());
    return {};
  }
  if (!req_user_id.empty() && req_user_id != authenticated.user_id) {
    resp->set_code(chirp::common::INVALID_PARAM);
    stamp();
    SendPacket(session, resp_id, pkt.sequence(), resp->SerializeAsString());
    return {};
  }
  return authenticated.user_id;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

// LOGIN_REQ: verifies the token (scaffold mode when no secret is configured),
// binds the session in the registry (kicking the previous session of the same
// (user, device) pair), marks presence ONLINE and tells the user's friends.
void HandleLogin(const std::shared_ptr<SocialState>& state,
                 const chirp::common::LoginTokenVerifier* verifier,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const chirp::gateway::Packet& pkt) {
  using chirp::common::Logger;

  chirp::auth::LoginRequest login_req;
  if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::auth::LoginResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string user_id;
  if (verifier && verifier->enabled()) {
    // Real mode: the token must be an HS256 JWT signed with the shared
    // secret, unexpired, with the login user in "sub".
    std::string verify_err;
    if (!verifier->Verify(login_req.token(), NowMs(), &user_id, &verify_err)) {
      Logger::Instance().Warn("social login rejected: " + verify_err);
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::AUTH_FAILED);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
  } else {
    // Scaffolding login: treat token as user_id.
    user_id = login_req.token();
  }

  if (user_id.empty()) {
    chirp::auth::LoginResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  chirp::auth::LoginResponse login_resp;
  login_resp.set_code(chirp::common::OK);
  login_resp.set_user_id(user_id);
  login_resp.set_session_id("social_session_" + RandomHex(8));
  login_resp.set_kick_previous(true);
  login_resp.mutable_kick()->set_reason("login from another device");
  login_resp.set_server_time(NowMs());

  auto old = chirp::network::BindAuthenticatedSession(
      state->registry, user_id, login_resp.session_id(),
      chirp::network::NormalizeDeviceId(login_req.device_id()), session);
  if (old && old.get() != session.get()) {
    KickSession(old, "login from another device");
  }

  SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());

  // Presence: flip to ONLINE but keep the user's own status message and
  // metadata (they belong to setPresence, not to the connection lifecycle).
  chirp::social::PresenceInfo info;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    info = state->presence[user_id];
    info.set_user_id(user_id);
    info.set_status(chirp::social::ONLINE);
    info.set_last_seen(NowMs());
    state->presence[user_id] = info;
  }

  BroadcastToFriends(state, user_id, chirp::gateway::PRESENCE_NOTIFY,
                     MakePresenceNotify(user_id, chirp::social::ONLINE, info.status_message())
                         .SerializeAsString());
}

void HandleAddFriend(const std::shared_ptr<SocialState>& state,
                     const std::shared_ptr<chirp::network::RedisClient>& redis,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  chirp::social::AddFriendRequest req;
  chirp::social::AddFriendResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::ADD_FRIEND_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  if (target.empty() || target == user_id) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string request_id;
  int64_t timestamp = NowMs();
  std::unordered_map<std::string, chirp::social::FriendRequest> pending_snapshot;
  {
    std::lock_guard<std::mutex> lock(state->mu);

    auto friends_it = state->friends.find(user_id);
    if (friends_it != state->friends.end() && friends_it->second.count(target)) {
      resp.set_code(chirp::common::INTERNAL_ERROR);  // already friends
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    auto blocked_it = state->blocked.find(target);
    if (blocked_it != state->blocked.end() && blocked_it->second.count(user_id)) {
      resp.set_code(chirp::common::INTERNAL_ERROR);  // target blocked the sender
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Idempotent duplicate: an identical open request returns the original id.
    for (const auto& [existing_id, existing] : state->pending_requests) {
      if (existing.from_user_id() == user_id && existing.to_user_id() == target) {
        request_id = existing_id;
        timestamp = existing.timestamp();
        break;
      }
    }

    if (request_id.empty()) {
      request_id = RandomHex(16);
      chirp::social::FriendRequest friend_req;
      friend_req.set_from_user_id(user_id);
      friend_req.set_to_user_id(target);
      friend_req.set_message(req.message());
      friend_req.set_timestamp(timestamp);
      state->pending_requests[request_id] = friend_req;
    }
    pending_snapshot = state->pending_requests;
  }

  PersistPending(redis, user_id, pending_snapshot);
  PersistPending(redis, target, pending_snapshot);

  // Tell the target (all their devices) about the new request.
  chirp::social::FriendRequestNotify notify;
  notify.set_request_id(request_id);
  notify.set_from_user_id(user_id);
  notify.set_message(req.message());
  notify.set_timestamp(timestamp);
  SendToUser(state, target, chirp::gateway::FRIEND_REQUEST_NOTIFY, notify.SerializeAsString());

  resp.set_code(chirp::common::OK);
  resp.set_request_id(request_id);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleFriendRequestAction(const std::shared_ptr<SocialState>& state,
                               const std::shared_ptr<chirp::network::RedisClient>& redis,
                               const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::gateway::Packet& pkt) {
  chirp::social::FriendRequestAction req;
  chirp::social::FriendRequestActionResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt,
                                          chirp::gateway::FRIEND_REQUEST_ACTION_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  std::string from_user_id;
  std::string to_user_id;
  std::unordered_set<std::string> from_friends;
  std::unordered_set<std::string> to_friends;
  std::unordered_map<std::string, chirp::social::FriendRequest> pending_snapshot;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->pending_requests.find(req.request_id());
    if (it == state->pending_requests.end() || it->second.to_user_id() != user_id) {
      // Unknown request, or not the user who received it.
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }

    from_user_id = it->second.from_user_id();
    to_user_id = it->second.to_user_id();
    state->pending_requests.erase(it);

    // The requester's mirror of this request (kept for the outgoing record)
    // is dropped too once the request is settled.
    for (auto mirror_it = state->pending_requests.begin();
         mirror_it != state->pending_requests.end();) {
      if (mirror_it->second.from_user_id() == to_user_id &&
          mirror_it->second.to_user_id() == from_user_id &&
          mirror_it->first != req.request_id()) {
        mirror_it = state->pending_requests.erase(mirror_it);
      } else {
        ++mirror_it;
      }
    }

    if (req.accept()) {
      state->friends[from_user_id].insert(to_user_id);
      state->friends[to_user_id].insert(from_user_id);
    }

    from_friends = state->friends[from_user_id];
    to_friends = state->friends[to_user_id];
    pending_snapshot = state->pending_requests;
  }

  PersistFriends(redis, from_user_id, from_friends);
  PersistFriends(redis, to_user_id, to_friends);
  PersistPending(redis, from_user_id, pending_snapshot);
  PersistPending(redis, to_user_id, pending_snapshot);

  if (req.accept()) {
    // Both sides learn who just became their friend: user_id is always the
    // OTHER party of the friendship (never the receiver - that would look
    // like a self-echo). username stays empty; the server holds no profiles.
    chirp::social::FriendAcceptedNotify to_requester;
    to_requester.set_user_id(to_user_id);
    to_requester.set_timestamp(NowMs());
    SendToUser(state, from_user_id, chirp::gateway::FRIEND_ACCEPTED_NOTIFY,
               to_requester.SerializeAsString());

    chirp::social::FriendAcceptedNotify to_acceptor;
    to_acceptor.set_user_id(from_user_id);
    to_acceptor.set_timestamp(NowMs());
    SendToUser(state, to_user_id, chirp::gateway::FRIEND_ACCEPTED_NOTIFY,
               to_acceptor.SerializeAsString());
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(),
             resp.SerializeAsString());
}

void HandleRemoveFriend(const std::shared_ptr<SocialState>& state,
                        const std::shared_ptr<chirp::network::RedisClient>& redis,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
  chirp::social::RemoveFriendRequest req;
  chirp::social::RemoveFriendResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::REMOVE_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::REMOVE_FRIEND_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.friend_user_id();
  if (target.empty() || target == user_id) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::REMOVE_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::unordered_set<std::string> user_friends;
  std::unordered_set<std::string> target_friends;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    // Symmetric delete; removing a non-friend is a no-op that still succeeds
    // (idempotent), so client retries never surface errors.
    if (auto it = state->friends.find(user_id); it != state->friends.end()) {
      it->second.erase(target);
      user_friends = it->second;
    }
    if (auto it = state->friends.find(target); it != state->friends.end()) {
      it->second.erase(user_id);
      target_friends = it->second;
    }
  }

  PersistFriends(redis, user_id, user_friends);
  PersistFriends(redis, target, target_friends);

  chirp::social::FriendRemovedNotify notify;
  notify.set_user_id(user_id);
  notify.set_timestamp(NowMs());
  SendToUser(state, target, chirp::gateway::FRIEND_REMOVED_NOTIFY, notify.SerializeAsString());

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::REMOVE_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetFriendList(const std::shared_ptr<SocialState>& state,
                         const std::shared_ptr<chirp::network::Session>& session,
                         const chirp::gateway::Packet& pkt) {
  chirp::social::GetFriendListRequest req;
  chirp::social::GetFriendListResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_FRIEND_LIST_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::GET_FRIEND_LIST_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  std::vector<std::string> ids;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    if (auto it = state->friends.find(user_id); it != state->friends.end()) {
      ids.assign(it->second.begin(), it->second.end());
    }
  }
  std::sort(ids.begin(), ids.end());  // stable pagination order

  resp.set_code(chirp::common::OK);
  resp.set_total_count(static_cast<int32_t>(ids.size()));
  const int32_t offset = std::max(0, req.offset());
  const int32_t limit = req.limit() <= 0 ? static_cast<int32_t>(ids.size()) : req.limit();
  for (int32_t i = offset; i < static_cast<int32_t>(ids.size()) && i < offset + limit; i++) {
    auto* info = resp.add_friends();
    info->set_user_id(ids[static_cast<size_t>(i)]);
    info->set_status(chirp::social::ACCEPTED);
  }

  SendPacket(session, chirp::gateway::GET_FRIEND_LIST_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetPendingRequests(const std::shared_ptr<SocialState>& state,
                              const std::shared_ptr<chirp::network::Session>& session,
                              const chirp::gateway::Packet& pkt) {
  chirp::social::GetPendingRequestsRequest req;
  chirp::social::GetPendingRequestsResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_PENDING_REQUESTS_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  const std::string user_id =
      RequireUser(state, session, pkt, chirp::gateway::GET_PENDING_REQUESTS_RESP, req.user_id(),
                  &resp);
  if (user_id.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    // Incoming side only; the outgoing mirror is the requester's record.
    // Each entry carries its request id so the receiver can act on it.
    for (const auto& [request_id, pending] : state->pending_requests) {
      if (pending.to_user_id() == user_id) {
        auto* entry = resp.add_requests();
        *entry = pending;
        entry->set_request_id(request_id);
      }
    }
  }

  resp.set_code(chirp::common::OK);
  SendPacket(session, chirp::gateway::GET_PENDING_REQUESTS_RESP, pkt.sequence(),
             resp.SerializeAsString());
}

void HandleBlockUser(const std::shared_ptr<SocialState>& state,
                     const std::shared_ptr<chirp::network::RedisClient>& redis,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  chirp::social::BlockUserRequest req;
  chirp::social::BlockUserResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::BLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::BLOCK_USER_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  if (target.empty() || target == user_id) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::BLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  bool removed_friend = false;
  std::unordered_set<std::string> user_friends;
  std::unordered_set<std::string> target_friends;
  std::unordered_set<std::string> user_blocked;
  std::unordered_map<std::string, chirp::social::FriendRequest> pending_snapshot;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    user_blocked = state->blocked[user_id];
    user_blocked.insert(target);

    // Blocking severs the friendship (and any open requests) both ways.
    if (auto it = state->friends.find(user_id); it != state->friends.end()) {
      removed_friend = it->second.erase(target) > 0;
      user_friends = it->second;
    }
    if (auto it = state->friends.find(target); it != state->friends.end()) {
      it->second.erase(user_id);
      target_friends = it->second;
    }
    for (auto pend_it = state->pending_requests.begin();
         pend_it != state->pending_requests.end();) {
      const auto& r = pend_it->second;
      const bool involved = (r.from_user_id() == user_id && r.to_user_id() == target) ||
                            (r.from_user_id() == target && r.to_user_id() == user_id);
      pend_it = involved ? state->pending_requests.erase(pend_it) : std::next(pend_it);
    }
    state->blocked[user_id] = user_blocked;
    pending_snapshot = state->pending_requests;
  }

  PersistBlocked(redis, user_id, user_blocked);
  PersistFriends(redis, user_id, user_friends);
  PersistFriends(redis, target, target_friends);
  PersistPending(redis, user_id, pending_snapshot);
  PersistPending(redis, target, pending_snapshot);

  if (removed_friend) {
    chirp::social::FriendRemovedNotify notify;
    notify.set_user_id(user_id);
    notify.set_timestamp(NowMs());
    SendToUser(state, target, chirp::gateway::FRIEND_REMOVED_NOTIFY, notify.SerializeAsString());
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::BLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleUnblockUser(const std::shared_ptr<SocialState>& state,
                       const std::shared_ptr<chirp::network::RedisClient>& redis,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::social::UnblockUserRequest req;
  chirp::social::UnblockUserResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::UNBLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::UNBLOCK_USER_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  if (target.empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::UNBLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::unordered_set<std::string> blocked_after;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    if (auto it = state->blocked.find(user_id); it != state->blocked.end()) {
      it->second.erase(target);
      if (it->second.empty()) {
        state->blocked.erase(it);
      } else {
        blocked_after = it->second;
      }
    }
  }

  // An empty snapshot still overwrites the Redis value - that is the delete.
  PersistBlocked(redis, user_id, blocked_after);

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::UNBLOCK_USER_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetBlockedList(const std::shared_ptr<SocialState>& state,
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::gateway::Packet& pkt) {
  chirp::social::GetBlockedListRequest req;
  chirp::social::GetBlockedListResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_BLOCKED_LIST_RESP, pkt.sequence(),
               resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::GET_BLOCKED_LIST_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    if (auto it = state->blocked.find(user_id); it != state->blocked.end()) {
      for (const auto& id : it->second) {
        resp.add_blocked_user_ids(id);
      }
    }
  }

  resp.set_code(chirp::common::OK);
  SendPacket(session, chirp::gateway::GET_BLOCKED_LIST_RESP, pkt.sequence(),
             resp.SerializeAsString());
}

void HandleSetPresence(const std::shared_ptr<SocialState>& state,
                       const std::shared_ptr<chirp::network::RedisClient>& redis,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::social::SetPresenceRequest req;
  chirp::social::SetPresenceResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::SET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::SET_PRESENCE_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  if (!chirp::social::PresenceStatus_IsValid(req.status())) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::SET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  chirp::social::PresenceInfo info;
  info.set_user_id(user_id);
  info.set_status(req.status());
  info.set_status_message(req.status_message());
  info.set_last_seen(NowMs());
  for (const auto& kv : req.metadata()) {
    (*info.mutable_metadata())[kv.first] = kv.second;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    state->presence[user_id] = info;
  }

  // Cross-instance presence keeps the 1h TTL: a stale record is better than
  // a permanent lie about someone's status.
  if (redis) {
    chirp::social::PresenceNotify stored = MakePresenceNotify(user_id, req.status(), req.status_message());
    for (const auto& kv : req.metadata()) {
      (*stored.mutable_metadata())[kv.first] = kv.second;
    }
    redis->SetEx("chirp:social:presence:" + user_id, stored.SerializeAsString(), 3600);
  }

  BroadcastToFriends(state, user_id, chirp::gateway::PRESENCE_NOTIFY,
                     MakePresenceNotify(user_id, req.status(), req.status_message())
                         .SerializeAsString());

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::SET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetPresence(const std::shared_ptr<SocialState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::social::GetPresenceRequest req;
  chirp::social::GetPresenceResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  if (chirp::network::GetAuthenticatedSession(state->registry, session).user_id.empty()) {
    resp.set_code(chirp::common::AUTH_FAILED);
    SendPacket(session, chirp::gateway::GET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  resp.set_code(chirp::common::OK);
  {
    std::lock_guard<std::mutex> lock(state->mu);
    for (const auto& user_id : req.user_ids()) {
      auto it = state->presence.find(user_id);
      if (it != state->presence.end()) {
        *resp.add_presences() = it->second;
      } else {
        // Unknown user: a plain offline placeholder.
        chirp::social::PresenceInfo info;
        info.set_user_id(user_id);
        info.set_status(chirp::social::OFFLINE);
        resp.add_presences()->CopyFrom(info);
      }
    }
  }

  SendPacket(session, chirp::gateway::GET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleDisconnect(const std::shared_ptr<SocialState>& state,
                      const std::shared_ptr<chirp::network::RedisClient>& redis,
                      const std::shared_ptr<chirp::network::Session>& session) {
  std::string user_id;
  const bool released =
      chirp::network::RemoveAuthenticatedSession(state->registry, session, &user_id);
  if (user_id.empty()) {
    return;  // never logged in on this connection
  }

  // Offline is only broadcast when the LAST device went away: a released slot
  // with remaining sessions means another device still holds the user online
  // (a kicked session also lands here with released==false, correctly silent).
  const bool went_offline = released && chirp::network::GetUserSessions(state->registry, user_id).empty();
  if (!went_offline) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->presence.find(user_id);
    if (it != state->presence.end()) {
      it->second.set_status(chirp::social::OFFLINE);
      it->second.set_last_seen(NowMs());
    }
  }

  const auto notify = MakePresenceNotify(user_id, chirp::social::OFFLINE, "");
  if (redis) {
    redis->SetEx("chirp:social:presence:" + user_id, notify.SerializeAsString(), 3600);
  }
  BroadcastToFriends(state, user_id, chirp::gateway::PRESENCE_NOTIFY, notify.SerializeAsString());
}

void HandlePacket(const std::shared_ptr<SocialState>& state,
                  const std::shared_ptr<chirp::network::RedisClient>& redis,
                  const chirp::common::LoginTokenVerifier* verifier,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  switch (pkt.msg_id()) {
  case chirp::gateway::LOGIN_REQ:
    HandleLogin(state, verifier, session, pkt);
    break;
  case chirp::gateway::ADD_FRIEND_REQ:
    HandleAddFriend(state, redis, session, pkt);
    break;
  case chirp::gateway::FRIEND_REQUEST_ACTION_REQ:
    HandleFriendRequestAction(state, redis, session, pkt);
    break;
  case chirp::gateway::REMOVE_FRIEND_REQ:
    HandleRemoveFriend(state, redis, session, pkt);
    break;
  case chirp::gateway::GET_FRIEND_LIST_REQ:
    HandleGetFriendList(state, session, pkt);
    break;
  case chirp::gateway::GET_PENDING_REQUESTS_REQ:
    HandleGetPendingRequests(state, session, pkt);
    break;
  case chirp::gateway::BLOCK_USER_REQ:
    HandleBlockUser(state, redis, session, pkt);
    break;
  case chirp::gateway::UNBLOCK_USER_REQ:
    HandleUnblockUser(state, redis, session, pkt);
    break;
  case chirp::gateway::GET_BLOCKED_LIST_REQ:
    HandleGetBlockedList(state, session, pkt);
    break;
  case chirp::gateway::SET_PRESENCE_REQ:
    HandleSetPresence(state, redis, session, pkt);
    break;
  case chirp::gateway::GET_PRESENCE_REQ:
    HandleGetPresence(state, session, pkt);
    break;
  case chirp::gateway::HEARTBEAT_PING: {
    chirp::gateway::HeartbeatPing ping;
    if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      Logger::Instance().Warn("failed to parse HeartbeatPing body");
      return;
    }

    chirp::gateway::HeartbeatPong pong;
    pong.set_timestamp(ping.timestamp());
    pong.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    break;
  }
  default:
    Logger::Instance().Warn("social: unhandled msg_id " + std::to_string(pkt.msg_id()));
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 8000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const std::string token_secret = GetArg(argc, argv, "--token_secret", "");

  Logger::Instance().Info("chirp_social starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port))) +
                          (token_secret.empty() ? " login=scaffold" : " login=jwt"));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  const chirp::common::LoginTokenVerifier token_verifier(token_secret);

  auto state = std::make_shared<SocialState>();
  LoadSocialState(redis, state);

  auto on_packet = [state, redis, &token_verifier](std::shared_ptr<chirp::network::Session> session,
                                                   std::string&& payload) {
    HandlePacket(state, redis, &token_verifier, session, std::move(payload));
  };
  auto on_disconnect = [state, redis](std::shared_ptr<chirp::network::Session> session) {
    HandleDisconnect(state, redis, session);
  };

  chirp::network::TcpServer server(io, port, on_packet, on_disconnect);
  chirp::network::WebSocketServer ws_server(io, ws_port, on_packet, on_disconnect);

  server.Start();
  ws_server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_social exited");
  return 0;
}
