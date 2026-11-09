#ifndef CHIRP_SOCIAL_DISPATCH_H_
#define CHIRP_SOCIAL_DISPATCH_H_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/login_token_verifier.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "proto/social.pb.h"

namespace chirp::social::direct {

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

  // 好友上线通知偏好（3025-3029）：user_id -> prefs。缺省全开——表里没有该
  // 用户的行就视为开，只有显式 SET 过的用户才落表（快照也按需写）。
  std::unordered_map<std::string, chirp::social::NotifyPrefs> notify_prefs;

  // Presence: user_id -> presence info
  std::unordered_map<std::string, chirp::social::PresenceInfo> presence;

  // 游戏在线状态 overlay (user_id -> set of game ids), fed by chat's
  // chirp:game_presence:events pub/sub. Kept separate from `presence`
  // because the two lifecycles differ: the social base flips with the
  // connection lifecycle, the overlay follows the game binding assertions
  // and survives a social logout (a player can be in-game with no
  // companion app connected — that is the exact surface this feature
  // exposes to friends).
  std::map<std::string, std::set<std::string>> game_presence;

  // Authenticated (user, device) -> session bindings; the single source of
  // truth for who is connected and where to deliver.
  std::shared_ptr<chirp::network::SessionRegistry> registry =
      std::make_shared<chirp::network::SessionRegistry>();
};


bool LoadSocialState(const std::shared_ptr<chirp::network::RedisClient>& redis, const std::shared_ptr<SocialState>& state);

void ConsumeGamePresenceEvent(const std::shared_ptr<SocialState>& state, const std::string& channel, const std::string& payload);

void HandleDisconnect(const std::shared_ptr<SocialState>& state, const std::shared_ptr<chirp::network::RedisClient>& redis, const std::shared_ptr<chirp::network::Session>& session);

void HandlePacket(const std::shared_ptr<SocialState>& state, const std::shared_ptr<chirp::network::RedisClient>& redis, const chirp::common::LoginTokenVerifier* verifier, const std::shared_ptr<chirp::network::Session>& session, std::string&& payload);

// Redis write-through helpers and presence resolution are declared here (not
// just defined in the .cc) so social_service_test.cc — which includes main.cc
// and drives the tables directly — resolves them via main.cc's using-block.
void PersistFriends(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_set<std::string>& friend_ids);

void PersistPending(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_map<std::string, chirp::social::FriendRequest>& pending);

void PersistBlocked(const std::shared_ptr<chirp::network::RedisClient>& redis,
                    const std::string& user_id,
                    const std::unordered_set<std::string>& blocked_ids);

chirp::social::PresenceInfo EffectivePresence(const std::shared_ptr<SocialState>& state, const std::string& user_id);

int64_t NowMs();

std::string RandomHex(size_t bytes);
}  // namespace chirp::social::direct

#endif  // CHIRP_SOCIAL_DISPATCH_H_
