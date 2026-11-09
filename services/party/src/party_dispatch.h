#ifndef CHIRP_PARTY_DISPATCH_H_
#define CHIRP_PARTY_DISPATCH_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/login_token_verifier.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "proto/party.pb.h"

namespace chirp::party::direct {

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


bool LoadPartyState(const std::shared_ptr<chirp::network::RedisClient>& redis, const std::shared_ptr<PartyState>& state);

void HandlePacket(const std::shared_ptr<PartyState>& state, const std::shared_ptr<chirp::network::RedisClient>& redis, const chirp::common::LoginTokenVerifier* verifier, const std::shared_ptr<chirp::network::Session>& session, std::string&& payload);

void HandleDisconnect(const std::shared_ptr<PartyState>& state, const std::shared_ptr<chirp::network::RedisClient>& redis, const std::shared_ptr<chirp::network::Session>& session);

int64_t NowMs();

std::string RandomHex(size_t bytes);
}  // namespace chirp::party::direct

#endif  // CHIRP_PARTY_DISPATCH_H_
