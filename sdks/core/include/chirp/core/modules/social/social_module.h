#ifndef CHIRP_CORE_MODULES_SOCIAL_SOCIAL_MODULE_H_
#define CHIRP_CORE_MODULES_SOCIAL_SOCIAL_MODULE_H_

#include <functional>
#include <string>
#include <vector>

#include "chirp/core/modules/social/social_events.h"

namespace chirp {
namespace core {
namespace modules {
namespace social {

// Presence status
enum class PresenceStatus {
  OFFLINE,
  ONLINE,
  AWAY,
  DND,           // Do not disturb
  IN_GAME,
  IN_BATTLE
};

// Friend status
enum class FriendStatus {
  NONE,
  PENDING,
  ACCEPTED,
  BLOCKED
};

// Friend information
struct Friend {
  std::string user_id;
  std::string username;
  std::string avatar_url;
  FriendStatus status;
  int64_t added_at;
};

// Presence information
struct Presence {
  std::string user_id;
  PresenceStatus status;
  std::string status_message;
  int64_t last_seen;
  std::string game_name;      // If in game
  std::string region;         // Region info
};

// Callback types
using AddFriendCallback = std::function<void(bool success, const std::string& request_id)>;
using FriendListCallback = std::function<void(const std::vector<Friend>& friends)>;
using PresenceCallback = std::function<void(const std::vector<Presence>& presences)>;
using SimpleCallback = std::function<void(bool success)>;

// Social module interface
class SocialModule {
public:
  virtual ~SocialModule() = default;

  // Friend management
  virtual void AddFriend(const std::string& user_id,
                        const std::string& message,
                        AddFriendCallback callback) = 0;

  virtual void AcceptFriendRequest(const std::string& request_id,
                                  SimpleCallback callback) = 0;

  virtual void DeclineFriendRequest(const std::string& request_id,
                                    SimpleCallback callback) = 0;

  virtual void RemoveFriend(const std::string& user_id,
                           SimpleCallback callback) = 0;

  virtual void GetFriendList(int32_t limit,
                            int32_t offset,
                            FriendListCallback callback) = 0;

  virtual void GetPendingRequests(std::function<void(const std::vector<FriendRequest>& requests)> callback) = 0;

  // Block management
  virtual void BlockUser(const std::string& user_id,
                        SimpleCallback callback) = 0;

  virtual void UnblockUser(const std::string& user_id,
                          SimpleCallback callback) = 0;

  virtual void GetBlockedList(std::function<void(const std::vector<std::string>& user_ids)> callback) = 0;

  // Presence management
  virtual void SetPresence(PresenceStatus status,
                          const std::string& status_message,
                          const std::string& game_name = "") = 0;

  virtual void GetPresence(const std::vector<std::string>& user_ids,
                          PresenceCallback callback) = 0;

  virtual void SubscribePresence(const std::vector<std::string>& user_ids) = 0;
  virtual void UnsubscribePresence(const std::vector<std::string>& user_ids) = 0;

  // Event callbacks
  virtual void SetPresenceCallback(PresenceChangeCallback callback) = 0;
  virtual void SetFriendRequestCallback(FriendRequestCallback callback) = 0;
  virtual void SetFriendAcceptedCallback(FriendAcceptedCallback callback) = 0;
  virtual void SetFriendRemovedCallback(FriendRemovedCallback callback) = 0;
};

} // namespace social
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_SOCIAL_SOCIAL_MODULE_H_
