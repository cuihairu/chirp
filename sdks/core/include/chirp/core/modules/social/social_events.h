#ifndef CHIRP_CORE_MODULES_SOCIAL_SOCIAL_EVENTS_H_
#define CHIRP_CORE_MODULES_SOCIAL_SOCIAL_EVENTS_H_

#include <functional>
#include <string>
#include <vector>

namespace chirp {
namespace core {
namespace modules {
namespace social {

// Friend request
struct FriendRequest {
  std::string request_id;
  std::string from_user_id;
  std::string from_username;
  std::string message;
  int64_t timestamp;
};

// Event callbacks
using PresenceChangeCallback = std::function<void(const std::string& user_id,
                                                 PresenceStatus status,
                                                 const std::string& status_message)>;

using FriendRequestCallback = std::function<void(const FriendRequest& request)>;

using FriendAcceptedCallback = std::function<void(const std::string& user_id,
                                                  const std::string& username)>;

using FriendRemovedCallback = std::function<void(const std::string& user_id)>;

} // namespace social
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_SOCIAL_SOCIAL_EVENTS_H_
