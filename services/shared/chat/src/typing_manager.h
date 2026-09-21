#ifndef CHIRP_SERVICES_CHAT_TYPING_MANAGER_H_
#define CHIRP_SERVICES_CHAT_TYPING_MANAGER_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Configuration for typing indicators
struct TypingConfig {
  int64_t typing_timeout_ms = 10000;      // 10 seconds of inactivity clears typing
  int64_t cooldown_ms = 1000;             // Minimum time between broadcast updates
  int32_t max_typing_users = 20;          // Max users to show in typing list
};

// Typing user data
struct TypingUser {
  std::string user_id;
  std::string username;
  int64_t last_typing_time = 0;
  int64_t last_broadcast_time = 0;
};

// Manages typing indicators for channels
class TypingManager {
public:
  explicit TypingManager(const TypingConfig& config = TypingConfig());
  ~TypingManager() = default;

  // User starts typing
  bool UserStartedTyping(const std::string& channel_id,
                        ChannelType channel_type,
                        const std::string& user_id,
                        const std::string& username,
                        TypingIndicator* out_notify = nullptr);

  // User stops typing
  bool UserStoppedTyping(const std::string& channel_id,
                        ChannelType channel_type,
                        const std::string& user_id);

  // Get currently typing users in a channel
  std::vector<std::string> GetTypingUsers(const std::string& channel_id,
                                         ChannelType channel_type);

  // Get typing indicator for broadcast
  bool GetTypingIndicator(const std::string& channel_id,
                         ChannelType channel_type,
                         const std::string& user_id,
                         TypingIndicator* out_indicator);

  // Clear expired typing users
  void CleanupExpired();

  // Get statistics
  size_t GetActiveChannelCount() const;
  size_t GetTotalTypingUserCount() const;

private:
  std::string GetChannelKey(const std::string& channel_id,
                           ChannelType channel_type) const;

  int64_t GetCurrentTimeMs() const;

  TypingConfig config_;
  mutable std::mutex mu_;

  // channel_key -> set of typing users
  std::unordered_map<std::string, std::unordered_map<std::string, TypingUser>> channel_typing_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_TYPING_MANAGER_H_
