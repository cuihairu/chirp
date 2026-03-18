#ifndef CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_SERVICE_H_
#define CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_SERVICE_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/notification.pb.h"

namespace chirp {
namespace notification {

// Notification payload
struct NotificationPayload {
  std::string title;
  std::string body;
  std::string icon;
  std::string image;              // Optional image URL
  std::string click_action;       // Deep link
  std::string sound;              // Sound to play
  std::string tag;                // Notification grouping
  int32_t badge = 0;              // iOS badge count
  std::unordered_map<std::string, std::string> data;  // Custom data

  // Channel mention info for priority
  bool is_mention = false;
  bool is_direct_message = false;
  int32_t unread_count = 0;
};

// Device registration
struct DeviceRegistration {
  std::string device_id;
  std::string user_id;
  std::string platform;            // "ios", "android", "web"
  std::string token;               // FCM/APNs token
  std::string app_version;
  std::string os_version;
  int64_t registered_at = 0;
  bool is_active = true;

  // iOS-specific
  std::string apns_environment;    // "development" or "production"
  std::string push_kit_token;      // VoIP token

  // Android-specific
  std::string fcm_token;

  mutable std::mutex mu;
};

// FCM configuration
struct FCMConfig {
  std::string server_key;
  std::string project_id;
  std::string endpoint = "https://fcm.googleapis.com/fcm/send";
  int32_t timeout_seconds = 10;
};

// APNs configuration
struct APNsConfig {
  std::string key_id;
  std::string team_id;
  std::string bundle_id;
  std::string private_key_path;
  std::string private_key_bytes;   // PEM format
  bool use_sandbox = false;        // Development vs Production
  std::string endpoint = "https://api.push.apple.com:443";
  int32_t timeout_seconds = 10;
};

// Notification service
class NotificationService {
public:
  NotificationService(const FCMConfig& fcm_config = FCMConfig(),
                     const APNsConfig& apns_config = APNsConfig());
  ~NotificationService() = default;

  // Device registration
  bool RegisterDevice(const DeviceRegistration& registration);
  bool UnregisterDevice(const std::string& device_id);
  bool UpdateDeviceToken(const std::string& device_id, const std::string& token);

  // Get user's devices
  std::vector<DeviceRegistration> GetUserDevices(const std::string& user_id);

  // Send notifications
  bool SendNotification(const std::string& user_id,
                       const NotificationPayload& payload);

  bool SendNotificationToDevice(const std::string& device_id,
                               const NotificationPayload& payload);

  bool SendNotificationToUsers(const std::vector<std::string>& user_ids,
                              const NotificationPayload& payload);

  // Broadcast to all users in a channel
  bool BroadcastToChannel(const std::string& channel_id,
                         const NotificationPayload& payload,
                         const std::vector<std::string>& exclude_users = {});

  // Message-specific notifications
  bool NotifyNewMessage(const std::string& to_user_id,
                       const std::string& from_user_id,
                       const std::string& from_username,
                       const std::string& message,
                       const std::string& channel_id);

  bool NotifyMention(const std::string& user_id,
                    const std::string& from_user_id,
                    const std::string& from_username,
                    const std::string& channel_id,
                    bool is_everyone = false);

  // Silent notification (for data sync)
  bool SendSilentNotification(const std::string& user_id,
                            const std::unordered_map<std::string, std::string>& data);

  // Badge management
  bool SetBadgeCount(const std::string& user_id, int32_t count);
  bool ClearBadge(const std::string& user_id);

  // Cooldown management (prevent notification spam)
  void SetNotificationCooldown(const std::string& user_id, int64_t duration_ms);
  bool IsOnCooldown(const std::string& user_id);

  // Cleanup
  void CleanupInactiveDevices(int64_t inactive_threshold_ms = 30 * 24 * 3600 * 1000);
  void CleanupExpiredCooldowns();

  // Statistics
  struct Stats {
    std::atomic<uint64_t> notifications_sent{0};
    std::atomic<uint64_t> notifications_failed{0};
    std::atomic<uint64_t> devices_registered{0};
    std::atomic<uint64_t> fcm_sent{0};
    std::atomic<uint64_t> apns_sent{0};
  };
  const Stats& GetStats() const { return stats_; }

private:
  bool SendFCM(const DeviceRegistration& device,
              const NotificationPayload& payload);

  bool SendAPNs(const DeviceRegistration& device,
               const NotificationPayload& payload);

  std::string BuildFCMPayload(const NotificationPayload& payload);
  std::string BuildAPNsPayload(const NotificationPayload& payload);

  std::string HTTPPost(const std::string& url,
                      const std::string& payload,
                      const std::unordered_map<std::string, std::string>& headers);

  int64_t GetCurrentTimeMs() const;

  FCMConfig fcm_config_;
  APNsConfig apns_config_;

  mutable std::mutex mu_;

  // Device storage
  std::unordered_map<std::string, std::shared_ptr<DeviceRegistration>> devices_;

  // User to devices index
  std::unordered_map<std::string, std::unordered_set<std::string>> user_to_devices_;

  // Notification cooldown (user_id -> last notification time)
  std::unordered_map<std::string, int64_t> cooldowns_;

  Stats stats_;
};

} // namespace notification
} // namespace chirp

#endif // CHIRP_SERVICES_NOTIFICATION_NOTIFICATION_SERVICE_H_
