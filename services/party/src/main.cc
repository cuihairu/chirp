// chirp_party: cross-game team-up signaling on the player plane (MsgID 7xxx).
// Invite-accept joining only (no join codes); STATE_CHANGED carries the full
// PartyInfo snapshot to every member so clients rebuild their view from one
// message. Membership is persisted to Redis (StoredParty snapshots); invites
// are ephemeral (memory-only, lazily expired).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <random>
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
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/party.pb.h"

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

constexpr int32_t kDefaultMaxMembers = 5;
constexpr int32_t kHardMaxMembers = 8;
constexpr int64_t kInviteTtlMs = 600000;  // 10 minutes, lazily enforced

// Redis snapshot keys, one per party. The value is the StoredParty message
// from proto/party.proto (storage schema, not wire protocol). Invites are
// deliberately not persisted.
constexpr const char* kPartyPrefix = "chirp:party:party:";

struct PartyMemberState {
  bool ready = false;
  int64_t joined_at = 0;
};

struct Party {
  std::string party_id;
  std::string leader_id;
  int32_t max_members = kDefaultMaxMembers;
  int64_t created_at = 0;
  // Ordered by user_id for stable snapshots; succession ties resolve to the
  // lexicographically smallest of the earliest joiners.
  std::map<std::string, PartyMemberState> members;
};

struct PendingInvite {
  std::string invite_id;
  std::string party_id;
  std::string from_user_id;
  std::string to_user_id;
  int64_t created_at = 0;
  int64_t expires_at = 0;
};

// Party service state. Lock discipline: never call a SessionRegistry free
// function while holding `mu` - the registry has its own lock and the two are
// always taken strictly one after the other (state mutation first, then
// registry lookups for delivery outside the lock). Redis persistence likewise
// happens outside the lock on a snapshot copied inside it.
struct PartyState {
  std::mutex mu;

  // party_id -> party
  std::unordered_map<std::string, std::shared_ptr<Party>> parties;

  // user_id -> party_id (a user is in at most one party). Derived from
  // `parties` on load, never persisted separately.
  std::unordered_map<std::string, std::string> user_to_party;

  // invite_id -> invite. Memory-only: expired invites are erased lazily on
  // touch, and everything here is lost on restart by design.
  std::unordered_map<std::string, PendingInvite> invites;

  // user_id -> invite ids this user already ACCEPTED. Powers the idempotent
  // second-device accept (a replayed invite returns the current party)
  // without letting arbitrary junk ids read as "already joined": invites
  // invalidated by the join or by a disband are NOT recorded here. Cleared
  // whenever the user leaves the party.
  std::unordered_map<std::string, std::unordered_set<std::string>> consumed_invites;

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
void SendToUser(const std::shared_ptr<PartyState>& state,
                const std::string& user_id,
                chirp::gateway::MsgID msg_id,
                const std::string& body) {
  for (const auto& sess : chirp::network::GetUserSessions(state->registry, user_id)) {
    SendPacket(sess, msg_id, 0, body);
  }
}

// ---------------------------------------------------------------------------
// Snapshots and Redis persistence. Best-effort write-through: the in-memory
// state is the working authority, Redis only restores it after a restart, so
// a failed write costs durability, never the in-flight request.
// ---------------------------------------------------------------------------

// Caller must hold state->mu (reads the party).
chirp::party::PartyInfo SnapshotOf(const Party& party) {
  chirp::party::PartyInfo info;
  info.set_party_id(party.party_id);
  info.set_leader_id(party.leader_id);
  info.set_max_members(party.max_members);
  info.set_created_at(party.created_at);
  for (const auto& [user_id, member] : party.members) {
    auto* out = info.add_members();
    out->set_user_id(user_id);
    out->set_ready(member.ready);
    out->set_joined_at(member.joined_at);
  }
  return info;
}

// Caller must NOT hold state->mu (does the network write).
void PersistParty(const std::shared_ptr<chirp::network::RedisClient>& redis,
                  const chirp::party::PartyInfo& snapshot) {
  if (!redis) {
    return;
  }
  chirp::party::StoredParty stored;
  stored.set_party_id(snapshot.party_id());
  stored.set_leader_id(snapshot.leader_id());
  stored.set_max_members(snapshot.max_members());
  stored.set_created_at(snapshot.created_at());
  for (const auto& member : snapshot.members()) {
    auto* out = stored.add_members();
    out->set_user_id(member.user_id());
    out->set_ready(member.ready());
    out->set_joined_at(member.joined_at());
  }
  if (!redis->Set(std::string(kPartyPrefix) + snapshot.party_id(), stored.SerializeAsString())) {
    chirp::common::Logger::Instance().Warn("party: failed to persist " + snapshot.party_id());
  }
}

void DeletePersistedParty(const std::shared_ptr<chirp::network::RedisClient>& redis,
                          const std::string& party_id) {
  if (redis && !redis->Del(std::string(kPartyPrefix) + party_id)) {
    chirp::common::Logger::Instance().Warn("party: failed to delete persisted " + party_id);
  }
}

// Restores parties from Redis snapshots. Returns false when Redis is
// unreachable (pure in-memory mode); a single corrupt or inconsistent key is
// skipped with a warning rather than failing the load. Rebuilds user_to_party
// and repairs a missing leader by promoting the earliest-joined member.
bool LoadPartyState(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::shared_ptr<PartyState>& state) {
  using chirp::common::Logger;
  if (!redis) {
    return false;
  }
  if (!redis->Command({"PING"})) {
    Logger::Instance().Warn("party: redis unreachable, starting with empty in-memory state");
    return false;
  }

  for (const auto& key : redis->Keys(std::string(kPartyPrefix) + "*")) {
    const auto value = redis->Get(key);
    chirp::party::StoredParty stored;
    if (!value || !stored.ParseFromString(*value)) {
      Logger::Instance().Warn("party: skipping corrupt party snapshot " + key);
      continue;
    }
    if (stored.members().empty()) {
      Logger::Instance().Warn("party: dropping empty party snapshot " + key);
      continue;
    }
    auto party = std::make_shared<Party>();
    party->party_id = stored.party_id();
    party->max_members = stored.max_members();
    party->created_at = stored.created_at();
    int64_t earliest = -1;
    std::string earliest_user;
    for (const auto& member : stored.members()) {
      (*party).members[member.user_id()] = PartyMemberState{member.ready(), member.joined_at()};
      if (earliest < 0 || member.joined_at() < earliest) {
        earliest = member.joined_at();
        earliest_user = member.user_id();
      }
    }
    // Leader must be a member; a stale leader id repairs to the earliest joiner.
    party->leader_id =
        party->members.count(stored.leader_id()) > 0 ? stored.leader_id() : earliest_user;

    std::lock_guard<std::mutex> lock(state->mu);
    state->parties[party->party_id] = party;
    for (const auto& [user_id, member] : party->members) {
      state->user_to_party[user_id] = party->party_id;
    }
  }
  return true;
}

// Guards a business handler: the caller must be a logged-in session, and a
// non-empty req.user_id must match the authenticated identity. Returns the
// authenticated user id, or empty after sending an error response.
template <typename RespT>
std::string RequireUser(const std::shared_ptr<PartyState>& state,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt,
                        chirp::gateway::MsgID resp_id,
                        const std::string& req_user_id,
                        RespT* resp) {
  const auto authenticated = chirp::network::GetAuthenticatedSession(state->registry, session);
  // Not every response message carries server_time (GetMyPartyResponse doesn't).
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

// LOGIN_REQ: verifies the token (scaffold mode when no secret is configured)
// and binds the session in the registry, kicking the previous session of the
// same (user, device) pair. Login never pushes party state — the client asks
// with GET_MY_PARTY after it connects.
void HandleLogin(const std::shared_ptr<PartyState>& state,
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
      Logger::Instance().Warn("party login rejected: " + verify_err);
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
  login_resp.set_session_id("party_session_" + RandomHex(8));
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
}

void HandleCreateParty(const std::shared_ptr<PartyState>& state,
                       const std::shared_ptr<chirp::network::RedisClient>& redis,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::party::CreatePartyRequest req;
  chirp::party::CreatePartyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::CREATE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::CREATE_PARTY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  int32_t max_members = req.max_members();
  if (max_members <= 0) {
    max_members = kDefaultMaxMembers;
  } else if (max_members > kHardMaxMembers) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::CREATE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  auto party = std::make_shared<Party>();
  party->party_id = "party_" + RandomHex(8);
  party->leader_id = user_id;
  party->max_members = max_members;
  party->created_at = NowMs();
  party->members[user_id] = PartyMemberState{false, NowMs()};

  {
    std::lock_guard<std::mutex> lock(state->mu);
    // A user can only lead one party at a time; creating while in a party
    // (scaffold clients can re-login without leaving) replaces the check with
    // a plain rejection to keep the invariant visible.
    if (state->user_to_party.count(user_id) > 0) {
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::CREATE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    state->parties[party->party_id] = party;
    state->user_to_party[user_id] = party->party_id;
  }

  resp.set_code(chirp::common::OK);
  *resp.mutable_party() = SnapshotOf(*party);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::CREATE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
  PersistParty(redis, resp.party());
}

void HandleDisbandParty(const std::shared_ptr<PartyState>& state,
                        const std::shared_ptr<chirp::network::RedisClient>& redis,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
  chirp::party::DisbandPartyRequest req;
  chirp::party::DisbandPartyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::DISBAND_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::DISBAND_PARTY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  std::vector<std::string> remaining;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::DISBAND_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (pit->second->leader_id != user_id) {
      resp.set_code(chirp::common::AUTH_FAILED);  // leader-only operation
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::DISBAND_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    remaining.reserve(pit->second->members.size());
    for (const auto& [member_id, member] : pit->second->members) {
      if (member_id != user_id) {
        remaining.push_back(member_id);
      }
      state->user_to_party.erase(member_id);
      state->consumed_invites.erase(member_id);
    }
    // Open invites die with the party.
    for (auto it = state->invites.begin(); it != state->invites.end();) {
      if (it->second.party_id == req.party_id()) {
        it = state->invites.erase(it);
      } else {
        ++it;
      }
    }
    state->parties.erase(pit);
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::DISBAND_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());

  chirp::party::PartyDisbandedNotify notify;
  notify.set_party_id(req.party_id());
  notify.set_actor_user_id(user_id);
  notify.set_timestamp(NowMs());
  for (const auto& member_id : remaining) {
    SendToUser(state, member_id, chirp::gateway::PARTY_DISBANDED_NOTIFY, notify.SerializeAsString());
  }
  DeletePersistedParty(redis, req.party_id());
}

void HandleInviteToParty(const std::shared_ptr<PartyState>& state,
                         const std::shared_ptr<chirp::network::Session>& session,
                         const chirp::gateway::Packet& pkt) {
  chirp::party::InviteToPartyRequest req;
  chirp::party::InviteToPartyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::INVITE_TO_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::INVITE_TO_PARTY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  PendingInvite invite;
  bool reinvited = false;
  chirp::party::PartyInfo party_snapshot;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::INVITE_TO_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (pit->second->members.count(user_id) == 0) {
      resp.set_code(chirp::common::AUTH_FAILED);  // any member may invite; you are not one
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::INVITE_TO_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (target.empty() || target == user_id || state->user_to_party.count(target) > 0 ||
        static_cast<int32_t>(pit->second->members.size()) >= pit->second->max_members) {
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::INVITE_TO_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Idempotent re-invite: an unexpired open invite for the same target is
    // reused (same id, refreshed expiry) instead of piling up duplicates.
    const int64_t now = NowMs();
    for (auto& [invite_id, existing] : state->invites) {
      if (existing.party_id == req.party_id() && existing.to_user_id == target &&
          existing.expires_at > now) {
        existing.from_user_id = user_id;
        existing.created_at = now;
        existing.expires_at = now + kInviteTtlMs;
        invite = existing;
        reinvited = true;
        break;
      }
    }
    if (!reinvited) {
      invite.invite_id = RandomHex(16);
      invite.party_id = req.party_id();
      invite.from_user_id = user_id;
      invite.to_user_id = target;
      invite.created_at = now;
      invite.expires_at = now + kInviteTtlMs;
      state->invites[invite.invite_id] = invite;
    }
    // Snapshot while still inside the lock so the notify carries a consistent
    // party view.
    party_snapshot = SnapshotOf(*pit->second);
  }

  resp.set_code(chirp::common::OK);
  resp.set_invite_id(invite.invite_id);
  resp.set_expires_at(invite.expires_at);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::INVITE_TO_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());

  chirp::party::InviteNotify notify;
  notify.set_invite_id(invite.invite_id);
  notify.set_from_user_id(invite.from_user_id);
  notify.set_expires_at(invite.expires_at);
  notify.set_timestamp(NowMs());
  *notify.mutable_party() = party_snapshot;
  SendToUser(state, target, chirp::gateway::INVITE_NOTIFY, notify.SerializeAsString());
}

void HandleAcceptInvite(const std::shared_ptr<PartyState>& state,
                        const std::shared_ptr<chirp::network::RedisClient>& redis,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
  chirp::party::AcceptInviteRequest req;
  chirp::party::AcceptInviteResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::ACCEPT_INVITE_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  std::shared_ptr<Party> party;
  PendingInvite invite;
  std::string inviter;
  const int64_t now = NowMs();
  {
    std::lock_guard<std::mutex> lock(state->mu);

    auto iit = state->invites.find(req.invite_id());
    if (iit == state->invites.end()) {
      // Unknown invite. A replay of an invite THIS user already accepted
      // (a second device accepting after the first one joined) reads as an
      // idempotent OK with the current party; anything else — junk ids,
      // invites invalidated by the join or by a disband — is "no such
      // invite".
      auto cit = state->consumed_invites.find(user_id);
      auto uit = state->user_to_party.find(user_id);
      if (cit != state->consumed_invites.end() && cit->second.count(req.invite_id()) > 0 &&
          uit != state->user_to_party.end()) {
        party = state->parties[uit->second];
        resp.set_code(chirp::common::OK);
        *resp.mutable_party() = SnapshotOf(*party);
        resp.set_server_time(NowMs());
        SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(),
                   resp.SerializeAsString());
        return;
      }
      resp.set_code(chirp::common::USER_NOT_FOUND);  // unknown invite
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (iit->second.to_user_id != user_id) {
      // Someone else's invite is just unknown from here.
      resp.set_code(chirp::common::USER_NOT_FOUND);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (iit->second.expires_at <= now) {
      state->invites.erase(iit);  // lazy expiry
      resp.set_code(chirp::common::USER_NOT_FOUND);  // expired invite
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    auto pit = state->parties.find(iit->second.party_id);
    if (pit == state->parties.end()) {
      // The party died while the invite was open; consume the invite.
      state->invites.erase(iit);
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party disbanded
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    auto uit = state->user_to_party.find(user_id);
    if (uit != state->user_to_party.end()) {
      if (uit->second == iit->second.party_id) {
        // Idempotent: a second device accepting after the first one joined.
        party = pit->second;
        resp.set_code(chirp::common::OK);
        *resp.mutable_party() = SnapshotOf(*party);
        resp.set_server_time(NowMs());
        SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(),
                   resp.SerializeAsString());
        return;
      }
      // In another party: the invite stays open so the user can retry after
      // leaving.
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    if (static_cast<int32_t>(pit->second->members.size()) >= pit->second->max_members) {
      // Full: keep the invite so the user can retry when a seat frees up.
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    invite = iit->second;
    inviter = invite.from_user_id;
    state->invites.erase(iit);  // consumed
    state->consumed_invites[user_id].insert(invite.invite_id);

    party = pit->second;
    party->members[user_id] = PartyMemberState{false, now};
    state->user_to_party[user_id] = party->party_id;
    // Joining one party invalidates every other open invite to this user.
    for (auto it = state->invites.begin(); it != state->invites.end();) {
      if (it->second.to_user_id == user_id) {
        it = state->invites.erase(it);
      } else {
        ++it;
      }
    }
  }

  resp.set_code(chirp::common::OK);
  *resp.mutable_party() = SnapshotOf(*party);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::ACCEPT_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());

  const auto snapshot = SnapshotOf(*party);
  chirp::party::PartyJoinedNotify joined;
  joined.set_party_id(party->party_id);
  joined.mutable_member()->set_user_id(user_id);
  joined.mutable_member()->set_ready(false);
  joined.mutable_member()->set_joined_at(now);
  joined.set_timestamp(NowMs());
  chirp::party::PartyStateChangedNotify changed;
  *changed.mutable_party() = snapshot;
  changed.set_timestamp(NowMs());
  for (const auto& member : snapshot.members()) {
    if (member.user_id() == user_id) {
      continue;  // the joiner has the resp snapshot; no JOINED echo
    }
    SendToUser(state, member.user_id(), chirp::gateway::PARTY_JOINED_NOTIFY, joined.SerializeAsString());
    SendToUser(state, member.user_id(), chirp::gateway::PARTY_STATE_CHANGED_NOTIFY,
               changed.SerializeAsString());
  }

  if (!inviter.empty()) {
    chirp::party::InviteResultNotify result;
    result.set_invite_id(invite.invite_id);
    result.set_target_user_id(user_id);
    result.set_accepted(true);
    result.set_timestamp(NowMs());
    SendToUser(state, inviter, chirp::gateway::INVITE_RESULT_NOTIFY, result.SerializeAsString());
  }

  PersistParty(redis, snapshot);
}

void HandleDeclineInvite(const std::shared_ptr<PartyState>& state,
                         const std::shared_ptr<chirp::network::Session>& session,
                         const chirp::gateway::Packet& pkt) {
  chirp::party::DeclineInviteRequest req;
  chirp::party::DeclineInviteResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::DECLINE_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::DECLINE_INVITE_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  PendingInvite invite;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto iit = state->invites.find(req.invite_id());
    if (iit == state->invites.end() || iit->second.to_user_id != user_id) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // unknown invite
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::DECLINE_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    invite = iit->second;
    state->invites.erase(iit);  // consumed either way (expired declines too)
    found = true;
  }
  (void)found;

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::DECLINE_INVITE_RESP, pkt.sequence(), resp.SerializeAsString());

  if (!invite.from_user_id.empty()) {
    chirp::party::InviteResultNotify result;
    result.set_invite_id(invite.invite_id);
    result.set_target_user_id(user_id);
    result.set_accepted(false);
    result.set_timestamp(NowMs());
    SendToUser(state, invite.from_user_id, chirp::gateway::INVITE_RESULT_NOTIFY,
               result.SerializeAsString());
  }
}

// Removes user_id from party_id inside state->mu, handling leader succession
// (earliest joined, ties by user_id) and silent auto-disband on the last
// member. Returns false when the party doesn't exist or the user isn't in it.
struct MemberRemoval {
  bool disbanded = false;
  std::string new_leader;
  std::vector<std::string> remaining;  // member ids still in after removal
};
bool RemoveMemberLocked(PartyState& state, const std::string& party_id, const std::string& user_id,
                        MemberRemoval* out) {
  auto pit = state.parties.find(party_id);
  if (pit == state.parties.end()) {
    return false;
  }
  auto& party = *pit->second;
  if (party.members.erase(user_id) == 0) {
    return false;
  }
  state.user_to_party.erase(user_id);
  state.consumed_invites.erase(user_id);  // leaving invalidates replayed accepts

  if (party.members.empty()) {
    // Silent disband: PARTY_DISBANDED isn't sent because nobody is left.
    state.parties.erase(pit);
    out->disbanded = true;
    return true;
  }

  out->remaining.reserve(party.members.size());
  for (const auto& [member_id, member] : party.members) {
    out->remaining.push_back(member_id);
  }
  if (party.leader_id == user_id) {
    // Succession: earliest joined wins, ties resolved by the map's user_id
    // order (strict < keeps the first of a tie).
    std::string successor;
    int64_t earliest = -1;
    for (const auto& [member_id, member] : party.members) {
      if (earliest < 0 || member.joined_at < earliest) {
        earliest = member.joined_at;
        successor = member_id;
      }
    }
    party.leader_id = successor;
    out->new_leader = successor;
  }
  return true;
}

// Shared notification tail for leave/kick/offline removals (all outside the
// state lock): LEFT to the remaining members, STATE_CHANGED when the snapshot
// they hold went stale (membership count or leader), persistence updates.
void FinishRemoval(const std::shared_ptr<PartyState>& state,
                   const std::shared_ptr<chirp::network::RedisClient>& redis,
                   const std::string& party_id,
                   const std::string& leaver_id,
                   const std::string& reason,
                   const MemberRemoval& removal) {
  chirp::party::PartyLeftNotify left;
  left.set_party_id(party_id);
  left.set_user_id(leaver_id);
  left.set_reason(reason);
  left.set_timestamp(NowMs());

  if (removal.disbanded) {
    DeletePersistedParty(redis, party_id);
    return;  // silent: nobody is left to tell
  }

  // Leader succession changes the snapshot everyone holds; build it once.
  bool send_state = false;
  chirp::party::PartyStateChangedNotify changed;
  if (!removal.new_leader.empty()) {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(party_id);
    if (pit != state->parties.end()) {
      *changed.mutable_party() = SnapshotOf(*pit->second);
      changed.set_timestamp(NowMs());
      send_state = true;
    }
  }

  for (const auto& member_id : removal.remaining) {
    SendToUser(state, member_id, chirp::gateway::PARTY_LEFT_NOTIFY, left.SerializeAsString());
    if (send_state) {
      SendToUser(state, member_id, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY,
                 changed.SerializeAsString());
    }
  }

  std::shared_ptr<Party> party;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(party_id);
    if (pit != state->parties.end()) {
      party = pit->second;
    }
  }
  if (party) {
    PersistParty(redis, SnapshotOf(*party));
  }
}

void HandleLeaveParty(const std::shared_ptr<PartyState>& state,
                      const std::shared_ptr<chirp::network::RedisClient>& redis,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const chirp::gateway::Packet& pkt) {
  chirp::party::LeavePartyRequest req;
  chirp::party::LeavePartyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LEAVE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::LEAVE_PARTY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  MemberRemoval removal;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LEAVE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (pit->second->members.count(user_id) == 0) {
      // In a different party or nowhere: distinguish the mismatch.
      auto uit = state->user_to_party.find(user_id);
      resp.set_code(uit != state->user_to_party.end() ? chirp::common::INVALID_PARAM
                                                      : chirp::common::USER_NOT_FOUND);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LEAVE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    if (!RemoveMemberLocked(*state, req.party_id(), user_id, &removal)) {
      resp.set_code(chirp::common::USER_NOT_FOUND);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LEAVE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
  }

  resp.set_code(chirp::common::OK);
  resp.set_party_disbanded(removal.disbanded);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::LEAVE_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());

  FinishRemoval(state, redis, req.party_id(), user_id, "left", removal);
}

void HandleKickMember(const std::shared_ptr<PartyState>& state,
                      const std::shared_ptr<chirp::network::RedisClient>& redis,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const chirp::gateway::Packet& pkt) {
  chirp::party::KickMemberRequest req;
  chirp::party::KickMemberResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::KICK_PARTY_MEMBER_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  MemberRemoval removal;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (pit->second->leader_id != user_id) {
      resp.set_code(chirp::common::AUTH_FAILED);  // leader-only operation
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (target == user_id) {
      resp.set_code(chirp::common::INVALID_PARAM);  // kick yourself = leave
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (pit->second->members.count(target) == 0) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // not a member
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (!RemoveMemberLocked(*state, req.party_id(), target, &removal)) {
      resp.set_code(chirp::common::USER_NOT_FOUND);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::KICK_PARTY_MEMBER_RESP, pkt.sequence(), resp.SerializeAsString());

  // The kicked user learns first (all devices), then the remaining members.
  chirp::party::PartyKickedNotify kicked;
  kicked.set_party_id(req.party_id());
  kicked.set_actor_user_id(user_id);
  kicked.set_timestamp(NowMs());
  SendToUser(state, target, chirp::gateway::PARTY_KICKED_NOTIFY, kicked.SerializeAsString());

  FinishRemoval(state, redis, req.party_id(), target, "kicked", removal);
}

void HandleTransferLeader(const std::shared_ptr<PartyState>& state,
                          const std::shared_ptr<chirp::network::RedisClient>& redis,
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::gateway::Packet& pkt) {
  chirp::party::TransferLeaderRequest req;
  chirp::party::TransferLeaderResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::TRANSFER_LEADER_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  const std::string& target = req.target_user_id();
  std::shared_ptr<Party> party;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (pit->second->leader_id != user_id) {
      resp.set_code(chirp::common::AUTH_FAILED);  // leader-only operation
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (target == user_id) {
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    if (pit->second->members.count(target) == 0) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // not a member
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(),
                 resp.SerializeAsString());
      return;
    }
    // Single critical section: validate then mutate, no partial failure and
    // therefore no rollback path.
    pit->second->leader_id = target;
    party = pit->second;
  }

  resp.set_code(chirp::common::OK);
  resp.set_leader_id(target);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::TRANSFER_LEADER_RESP, pkt.sequence(), resp.SerializeAsString());

  // Snapshot to every member INCLUDING the actor: snapshot sync is
  // idempotent, the actor's own resp only carries the new leader id.
  chirp::party::PartyStateChangedNotify changed;
  *changed.mutable_party() = SnapshotOf(*party);
  changed.set_timestamp(NowMs());
  for (const auto& member : changed.party().members()) {
    SendToUser(state, member.user_id(), chirp::gateway::PARTY_STATE_CHANGED_NOTIFY,
               changed.SerializeAsString());
  }

  PersistParty(redis, changed.party());
}

void HandleSetReady(const std::shared_ptr<PartyState>& state,
                    const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt) {
  chirp::party::SetReadyRequest req;
  chirp::party::SetReadyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::SET_READY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::SET_READY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  std::shared_ptr<Party> party;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto pit = state->parties.find(req.party_id());
    if (pit == state->parties.end()) {
      resp.set_code(chirp::common::USER_NOT_FOUND);  // party not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::SET_READY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto mit = pit->second->members.find(user_id);
    if (mit == pit->second->members.end()) {
      auto uit = state->user_to_party.find(user_id);
      resp.set_code(uit != state->user_to_party.end() ? chirp::common::INVALID_PARAM
                                                      : chirp::common::USER_NOT_FOUND);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::SET_READY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    mit->second.ready = req.ready();
    party = pit->second;
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::SET_READY_RESP, pkt.sequence(), resp.SerializeAsString());

  // Snapshot to every member INCLUDING the actor (idempotent snapshot sync —
  // a deliberate departure from voice's actor-excluded state notify).
  chirp::party::PartyStateChangedNotify changed;
  *changed.mutable_party() = SnapshotOf(*party);
  changed.set_timestamp(NowMs());
  for (const auto& member : changed.party().members()) {
    SendToUser(state, member.user_id(), chirp::gateway::PARTY_STATE_CHANGED_NOTIFY,
               changed.SerializeAsString());
  }

  PersistParty(redis, changed.party());
}

void HandleGetMyParty(const std::shared_ptr<PartyState>& state,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const chirp::gateway::Packet& pkt) {
  chirp::party::GetMyPartyRequest req;
  chirp::party::GetMyPartyResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_MY_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const std::string user_id = RequireUser(state, session, pkt, chirp::gateway::GET_MY_PARTY_RESP,
                                          req.user_id(), &resp);
  if (user_id.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto uit = state->user_to_party.find(user_id);
    if (uit == state->user_to_party.end()) {
      resp.set_code(chirp::common::OK);
      resp.set_in_party(false);
      SendPacket(session, chirp::gateway::GET_MY_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    resp.set_code(chirp::common::OK);
    resp.set_in_party(true);
    *resp.mutable_party() = SnapshotOf(*state->parties[uit->second]);
  }
  SendPacket(session, chirp::gateway::GET_MY_PARTY_RESP, pkt.sequence(), resp.SerializeAsString());
}

// A member whose last device disconnects leaves the party (reason "offline"),
// with the same succession / silent-disband semantics as an explicit leave.
void HandleDisconnect(const std::shared_ptr<PartyState>& state,
                      const std::shared_ptr<chirp::network::RedisClient>& redis,
                      const std::shared_ptr<chirp::network::Session>& session) {
  std::string user_id;
  const bool released =
      chirp::network::RemoveAuthenticatedSession(state->registry, session, &user_id);
  if (user_id.empty()) {
    return;  // never logged in on this connection
  }

  // Leave only fires when the LAST device went away: a released slot with
  // remaining sessions means another device still holds the user in (a
  // kicked session also lands here with released==false, correctly silent).
  const bool went_offline = released && chirp::network::GetUserSessions(state->registry, user_id).empty();
  if (!went_offline) {
    return;
  }

  std::string party_id;
  MemberRemoval removal;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto uit = state->user_to_party.find(user_id);
    if (uit == state->user_to_party.end()) {
      return;
    }
    party_id = uit->second;
    if (!RemoveMemberLocked(*state, party_id, user_id, &removal)) {
      return;
    }
  }

  FinishRemoval(state, redis, party_id, user_id, "offline", removal);
}

void HandlePacket(const std::shared_ptr<PartyState>& state,
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
  case chirp::gateway::CREATE_PARTY_REQ:
    HandleCreateParty(state, redis, session, pkt);
    break;
  case chirp::gateway::DISBAND_PARTY_REQ:
    HandleDisbandParty(state, redis, session, pkt);
    break;
  case chirp::gateway::INVITE_TO_PARTY_REQ:
    HandleInviteToParty(state, session, pkt);
    break;
  case chirp::gateway::ACCEPT_INVITE_REQ:
    HandleAcceptInvite(state, redis, session, pkt);
    break;
  case chirp::gateway::DECLINE_INVITE_REQ:
    HandleDeclineInvite(state, session, pkt);
    break;
  case chirp::gateway::LEAVE_PARTY_REQ:
    HandleLeaveParty(state, redis, session, pkt);
    break;
  case chirp::gateway::KICK_PARTY_MEMBER_REQ:
    HandleKickMember(state, redis, session, pkt);
    break;
  case chirp::gateway::TRANSFER_LEADER_REQ:
    HandleTransferLeader(state, redis, session, pkt);
    break;
  case chirp::gateway::SET_READY_REQ:
    HandleSetReady(state, redis, session, pkt);
    break;
  case chirp::gateway::GET_MY_PARTY_REQ:
    HandleGetMyParty(state, session, pkt);
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
    Logger::Instance().Warn("party: unhandled msg_id " + std::to_string(pkt.msg_id()));
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 7500);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const std::string token_secret = GetArg(argc, argv, "--token_secret", "");

  Logger::Instance().Info("chirp_party starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port))) +
                          (token_secret.empty() ? " login=scaffold" : " login=jwt"));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  const chirp::common::LoginTokenVerifier token_verifier(token_secret);

  auto state = std::make_shared<PartyState>();
  LoadPartyState(redis, state);  // before listen: connections see restored state

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
  Logger::Instance().Info("chirp_party exited");
  return 0;
}
