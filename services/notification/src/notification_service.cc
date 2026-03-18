#include "notification_service.h"

#include <chrono>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace chirp {
namespace notification {

namespace {

// Platform constants
constexpr const char* kPlatformIOS = "ios";
constexpr const char* kPlatformAndroid = "android";
constexpr const char* kPlatformWeb = "web";

// Default notification templates
std::string GetMessageNotificationTitle(const std::string& from_username) {
  return from_username;
}

std::string GetMessageNotificationBody(const std::string& message, size_t max_length = 100) {
  if (message.length() <= max_length) {
    return message;
  }
  return message.substr(0, max_length - 3) + "...";
}

std::string GetMentionNotificationBody(const std::string& from_username,
                                      const std::string& channel_name) {
  return from_username + " mentioned you in " + channel_name;
}

std::string GetCallNotificationTitle() {
  return "Incoming Call";
}

} // namespace

NotificationService::NotificationService(const FCMConfig& fcm_config,
                                       const APNsConfig& apns_config)
    : fcm_config_(fcm_config), apns_config_(apns_config) {}

int64_t NotificationService::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

bool NotificationService::RegisterDevice(const DeviceRegistration& registration) {
  std::lock_guard<std::mutex> lock(mu_);

  // Create or update device registration
  auto device = std::make_shared<DeviceRegistration>(registration);
  device->registered_at = GetCurrentTimeMs();
  device->is_active = true;

  devices_[device->device_id] = device;
  user_to_devices_[device->user_id].insert(device->device_id);

  stats_.devices_registered++;

  return true;
}

bool NotificationService::UnregisterDevice(const std::string& device_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = devices_.find(device_id);
  if (it == devices_.end()) {
    return false;
  }

  const auto& device = it->second;

  // Remove from user index
  auto user_it = user_to_devices_.find(device->user_id);
  if (user_it != user_to_devices_.end()) {
    user_it->second.erase(device_id);
    if (user_it->second.empty()) {
      user_to_devices_.erase(user_it);
    }
  }

  devices_.erase(it);

  return true;
}

bool NotificationService::UpdateDeviceToken(const std::string& device_id,
                                          const std::string& token) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = devices_.find(device_id);
  if (it == devices_.end()) {
    return false;
  }

  auto& device = it->second;
  std::lock_guard<std::mutex> device_lock(device->mu);

  // Update token based on platform
  if (device->platform == kPlatformAndroid || device->platform == kPlatformWeb) {
    device->fcm_token = token;
  } else if (device->platform == kPlatformIOS) {
    device->apns_token = token;
  }

  return true;
}

std::vector<DeviceRegistration> NotificationService::GetUserDevices(
    const std::string& user_id) {
  std::vector<DeviceRegistration> result;

  std::lock_guard<std::mutex> lock(mu_);

  auto it = user_to_devices_.find(user_id);
  if (it == user_to_devices_.end()) {
    return result;
  }

  for (const auto& device_id : it->second) {
    auto device_it = devices_.find(device_id);
    if (device_it != devices_.end()) {
      result.push_back(*device_it->second);
    }
  }

  return result;
}

bool NotificationService::SendNotification(const std::string& user_id,
                                         const NotificationPayload& payload) {
  auto devices = GetUserDevices(user_id);

  if (devices.empty()) {
    return false;
  }

  bool success = false;
  for (const auto& device : devices) {
    if (SendNotificationToDevice(device.device_id, payload)) {
      success = true;
    }
  }

  return success;
}

bool NotificationService::SendNotificationToDevice(const std::string& device_id,
                                                  const NotificationPayload& payload) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = devices_.find(device_id);
  if (it == devices_.end()) {
    return false;
  }

  const auto& device = it->second;

  if (!device->is_active) {
    return false;
  }

  // Check cooldown
  if (IsOnCooldown(device->user_id)) {
    return false;
  }

  bool success = false;

  // Send based on platform
  if (device->platform == kPlatformAndroid || device->platform == kPlatformWeb) {
    success = SendFCM(*device, payload);
    if (success) stats_.fcm_sent++;
  } else if (device->platform == kPlatformIOS) {
    success = SendAPNs(*device, payload);
    if (success) stats_.apns_sent++;
  }

  if (success) {
    stats_.notifications_sent++;
    SetNotificationCooldown(device->user_id, 60000);  // 1 minute default
  } else {
    stats_.notifications_failed++;
  }

  return success;
}

bool NotificationService::SendNotificationToUsers(
    const std::vector<std::string>& user_ids,
    const NotificationPayload& payload) {
  bool success = false;

  for (const auto& user_id : user_ids) {
    if (SendNotification(user_id, payload)) {
      success = true;
    }
  }

  return success;
}

bool NotificationService::BroadcastToChannel(
    const std::string& channel_id,
    const NotificationPayload& payload,
    const std::vector<std::string>& exclude_users) {

  // In production, get channel members from social service
  // For now, return true (placeholder)
  return true;
}

bool NotificationService::NotifyNewMessage(const std::string& to_user_id,
                                          const std::string& from_user_id,
                                          const std::string& from_username,
                                          const std::string& message,
                                          const std::string& channel_id) {
  NotificationPayload payload;
  payload.title = GetMessageNotificationTitle(from_username);
  payload.body = GetMessageNotificationBody(message);
  payload.sound = "default";
  payload.tag = channel_id;
  payload.is_direct_message = true;

  // Add click action
  payload.click_action = "chirp://chat/" + channel_id;

  // Add data
  payload.data["type"] = "message";
  payload.data["from_user_id"] = from_user_id;
  payload.data["channel_id"] = channel_id;

  return SendNotification(to_user_id, payload);
}

bool NotificationService::NotifyMention(const std::string& user_id,
                                       const std::string& from_user_id,
                                       const std::string& from_username,
                                       const std::string& channel_id,
                                       bool is_everyone) {
  NotificationPayload payload;
  payload.title = from_username;
  payload.body = GetMentionNotificationBody(from_username, channel_id);
  payload.sound = "default";
  payload.tag = channel_id;
  payload.is_mention = true;

  if (is_everyone) {
    payload.body = from_username + " mentioned @everyone in " + channel_id;
  }

  payload.click_action = "chirp://chat/" + channel_id;

  payload.data["type"] = "mention";
  payload.data["from_user_id"] = from_user_id;
  payload.data["channel_id"] = channel_id;

  return SendNotification(user_id, payload);
}

bool NotificationService::SendSilentNotification(
    const std::string& user_id,
    const std::unordered_map<std::string, std::string>& data) {

  auto devices = GetUserDevices(user_id);

  bool success = false;
  for (const auto& device : devices) {
    NotificationPayload payload;
    payload.data = data;
    payload.sound = "";  // Silent

    if (SendNotificationToDevice(device.device_id, payload)) {
      success = true;
    }
  }

  return success;
}

bool NotificationService::SetBadgeCount(const std::string& user_id, int32_t count) {
  auto devices = GetUserDevices(user_id);

  bool success = false;
  for (const auto& device : devices) {
    if (device.platform == kPlatformIOS) {
      NotificationPayload payload;
      payload.badge = count;
      payload.data["type"] = "badge_update";
      payload.data["badge"] = std::to_string(count);

      if (SendNotificationToDevice(device.device_id, payload)) {
        success = true;
      }
    }
  }

  return success;
}

bool NotificationService::ClearBadge(const std::string& user_id) {
  return SetBadgeCount(user_id, 0);
}

void NotificationService::SetNotificationCooldown(const std::string& user_id,
                                                int64_t duration_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  cooldowns_[user_id] = GetCurrentTimeMs() + duration_ms;
}

bool NotificationService::IsOnCooldown(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = cooldowns_.find(user_id);
  if (it == cooldowns_.end()) {
    return false;
  }

  if (GetCurrentTimeMs() >= it->second) {
    cooldowns_.erase(it);
    return false;
  }

  return true;
}

void NotificationService::CleanupInactiveDevices(int64_t inactive_threshold_ms) {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t cutoff = GetCurrentTimeMs() - inactive_threshold_ms;

  for (auto it = devices_.begin(); it != devices_.end();) {
    const auto& device = it->second;
    std::lock_guard<std::mutex> device_lock(device->mu);

    if (device->registered_at < cutoff && !device->is_active) {
      // Remove from user index
      auto user_it = user_to_devices_.find(device->user_id);
      if (user_it != user_to_devices_.end()) {
        user_it->second.erase(device->device_id);
        if (user_it->second.empty()) {
          user_to_devices_.erase(user_it);
        }
      }

      it = devices_.erase(it);
    } else {
      ++it;
    }
  }
}

void NotificationService::CleanupExpiredCooldowns() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();

  for (auto it = cooldowns_.begin(); it != cooldowns_.end();) {
    if (now >= it->second) {
      it = cooldowns_.erase(it);
    } else {
      ++it;
    }
  }
}

// HTTP POST helper (simplified - use libcurl in production)
std::string NotificationService::HTTPPost(
    const std::string& url,
    const std::string& payload,
    const std::unordered_map<std::string, std::string>& headers) {

  // In production, use libcurl or similar
  // For demo, return empty response
  return "";
}

bool NotificationService::SendFCM(const DeviceRegistration& device,
                                 const NotificationPayload& payload) {
  // Build FCM payload
  std::string fcm_payload = BuildFCMPayload(payload);

  // Build headers
  std::unordered_map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  headers["Authorization"] = "key=" + fcm_config_.server_key;

  // Send HTTP POST
  std::string response = HTTPPost(fcm_config_.endpoint, fcm_payload, headers);

  // In production, parse response to determine success
  return !response.empty() || device.fcm_token.empty();  // Don't fail if no token
}

bool NotificationService::SendAPNs(const DeviceRegistration& device,
                                  const NotificationPayload& payload) {
  // Build APNs payload
  std::string apns_payload = BuildAPNsPayload(payload);

  // Build headers
  std::unordered_map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  headers["apns-topic"] = apns_config_.bundle_id;
  headers["apns-push-type"] = "alert";
  headers["apns-priority"] = "10";
  headers["apns-collapse-id"] = payload.tag;

  std::string endpoint = apns_config_.use_sandbox
      ? "https://api.development.push.apple.com:443"
      : "https://api.push.apple.com:443";

  // Send HTTP/2 POST (requires HTTP/2 support)
  std::string response = HTTPPost(endpoint, apns_payload, headers);

  // In production, parse response to determine success
  return !response.empty() || device.apns_token.empty();
}

std::string NotificationService::BuildFCMPayload(const NotificationPayload& payload) {
  // Build JSON payload for FCM
  std::ostringstream ss;

  ss << "{"
     << "\"to\": \"" << "" << "\","  // Device token
     << "\"notification\": {"
     << "\"title\": \"" << payload.title << "\","
     << "\"body\": \"" << payload.body << "\"";

  if (!payload.icon.empty()) {
    ss << ",\"icon\": \"" << payload.icon << "\"";
  }
  if (!payload.sound.empty()) {
    ss << ",\"sound\": \"" << payload.sound << "\"";
  }

  ss << "},\"data\": {";

  bool first = true;
  for (const auto& [key, value] : payload.data) {
    if (!first) ss << ",";
    ss << "\"" << key << "\": \"" << value << "\"";
    first = false;
  }

  ss << "}}";

  return ss.str();
}

std::string NotificationService::BuildAPNsPayload(const NotificationPayload& payload) {
  // Build JSON payload for APNs
  std::ostringstream ss;

  ss << "{"
     << "\"aps\": {"
     << "\"alert\": {"
     << "\"title\": \"" << payload.title << "\","
     << "\"body\": \"" << payload.body << "\""
     << "},";

  if (payload.badge > 0) {
    ss << "\"badge\": " << payload.badge << ",";
  }

  if (!payload.sound.empty()) {
    ss << "\"sound\": \"" << payload.sound << "\",";
  }

  ss << "\"content-available\": 1"
     << "},";

  // Add custom data
  ss << "\"chirp_data\": {";
  bool first = true;
  for (const auto& [key, value] : payload.data) {
    if (!first) ss << ",";
    ss << "\"" << key << "\": \"" << value << "\"";
    first = false;
  }
  ss << "}}";

  return ss.str();
}

} // namespace notification
} // namespace chirp
