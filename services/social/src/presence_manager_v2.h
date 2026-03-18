#ifndef CHIRP_SERVICES_SOCIAL_PRESENCE_MANAGER_V2_H_
#define CHIRP_SERVICES_SOCIAL_PRESENCE_MANAGER_V2_H_

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/social.pb.h"

namespace chirp {
namespace social {

// User presence status
enum class PresenceStatus {
  OFFLINE = 0,
  ONLINE = 1,
  IDLE = 2,         // Away due to inactivity
  DO_NOT_DISTURB = 3,
  INVISIBLE = 4,    // Appears offline but is actually online
  IN_GAME = 5,
  IN_VOICE = 6,
  IN_CALL = 7
};

// User activity (for presence detection)
enum class UserActivity {
  TYPING = 0,
  MOVING_MOUSE = 1,
  INTERACTING = 2,
  IDLE = 3,
  AWAY = 4
};

// Custom status
struct CustomStatus {
  std::string text;           // User's custom status message
  std::string emoji;          // Optional emoji
  int64_t expires_at = 0;     // Optional expiration
};

// Presence data for a user
struct PresenceData {
  std::string user_id;
  PresenceStatus status = PresenceStatus::OFFLINE;
  std::string status_message;   // Activity description (e.g., "Playing Game")
  std::string activity_type;    // "game", "music", "streaming", etc.
  std::string activity_details; // Game name, song title, etc.
  int64_t last_seen = 0;        // Last activity timestamp
  int64_t online_since = 0;      // When user came online
  std::string device_id;        // Current device
  std::string client_type;      // "desktop", "mobile", "web"

  CustomStatus custom_status;

  // For multiple devices
  std::unordered_map<std::string, PresenceStatus> device_status;  // device_id -> status

  mutable std::mutex mu;
};

// Configuration
struct PresenceConfig {
  int64_t idle_timeout_ms = 300000;         // 5 minutes until idle
  int64_t offline_timeout_ms = 600000;      // 10 minutes until offline
  int64_t online_broadcast_interval_ms = 30000;  // Broadcast every 30s
  int32_t max_friends_to_notify = 100;      // Limit friend notifications
};

// Presence change callback
using PresenceChangeCallback = std::function<void(
    const std::string& user_id,
    PresenceStatus old_status,
    PresenceStatus new_status)>;

// Enhanced presence manager
class PresenceManagerV2 {
public:
  explicit PresenceManagerV2(const PresenceConfig& config = PresenceConfig());
  ~PresenceManagerV2() = default;

  // Update user presence
  bool UpdatePresence(const std::string& user_id,
                     PresenceStatus status,
                     const std::string& device_id = "",
                     const std::string& client_type = "");

  // Update activity (heartbeat)
  bool RecordActivity(const std::string& user_id,
                    UserActivity activity,
                    const std::string& device_id = "");

  // Set custom status
  bool SetCustomStatus(const std::string& user_id,
                      const std::string& text,
                      const std::string& emoji = "",
                      int64_t duration_ms = 0);

  // Clear custom status
  bool ClearCustomStatus(const std::string& user_id);

  // Set activity (e.g., playing game)
  bool SetActivity(const std::string& user_id,
                 const std::string& activity_type,
                 const std::string& activity_details = "");

  // Get user presence
  bool GetPresence(const std::string& user_id, PresenceData* out_data);

  // Get presence for multiple users (bulk)
  std::unordered_map<std::string, PresenceData> GetPresenceBatch(
      const std::vector<std::string>& user_ids);

  // Get online friends
  std::vector<std::string> GetOnlineFriends(
      const std::string& user_id,
      const std::unordered_set<std::string>& friend_ids);

  // Presence callbacks
  void SetPresenceChangeCallback(PresenceChangeCallback callback) {
    presence_change_callback_ = std::move(callback);
  }

  // Broadcast presence to Redis (for other services)
  std::string SerializePresence(const PresenceData& data);
  bool DeserializePresence(const std::string& json, PresenceData* out_data);

  // Session management
  bool RegisterSession(const std::string& user_id,
                     const std::string& session_id,
                     const std::string& device_id);

  bool UnregisterSession(const std::string& user_id,
                       const std::string& session_id);

  // Get all sessions for user
  std::vector<std::string> GetUserSessions(const std::string& user_id);

  // Cleanup
  void CleanupIdleUsers();
  void CleanupOfflineUsers();

  // Statistics
  size_t GetOnlineUserCount() const;
  size_t GetTotalSessionCount() const;

  // Status conversion
  static std::string StatusToString(PresenceStatus status);
  static PresenceStatus StringToStatus(const std::string& str);

private:
  PresenceStatus ComputeOverallStatus(const PresenceData& data);
  void NotifyPresenceChange(const std::string& user_id,
                           PresenceStatus old_status,
                           PresenceStatus new_status);
  int64_t GetCurrentTimeMs() const;

  PresenceConfig config_;
  mutable std::mutex mu_;

  // User presence storage
  std::unordered_map<std::string, std::shared_ptr<PresenceData>> user_presence_;

  // Session tracking
  std::unordered_map<std::string, std::unordered_set<std::string>> user_sessions_;

  // Device tracking
  std::unordered_map<std::string, std::string> session_to_device_;

  // Callback
  PresenceChangeCallback presence_change_callback_;
};

} // namespace social
} // namespace chirp

#endif // CHIRP_SERVICES_SOCIAL_PRESENCE_MANAGER_V2_H_
