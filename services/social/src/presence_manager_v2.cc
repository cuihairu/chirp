#include "presence_manager_v2.h"

#include <algorithm>
#include <sstream>

namespace chirp {
namespace social {

namespace {

// Status string conversion
const char* kStatusStrings[] = {
  "offline",
  "online",
  "idle",
  "do_not_disturb",
  "invisible",
  "in_game",
  "in_voice",
  "in_call"
};

} // namespace

PresenceManagerV2::PresenceManagerV2(const PresenceConfig& config)
    : config_(config) {}

int64_t PresenceManagerV2::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string PresenceManagerV2::StatusToString(PresenceStatus status) {
  int idx = static_cast<int>(status);
  if (idx >= 0 && idx < sizeof(kStatusStrings) / sizeof(kStatusStrings[0])) {
    return kStatusStrings[idx];
  }
  return "offline";
}

PresenceStatus PresenceManagerV2::StringToStatus(const std::string& str) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  for (size_t i = 0; i < sizeof(kStatusStrings) / sizeof(kStatusStrings[0]); ++i) {
    if (lower == kStatusStrings[i]) {
      return static_cast<PresenceStatus>(i);
    }
  }
  return PresenceStatus::OFFLINE;
}

bool PresenceManagerV2::UpdatePresence(const std::string& user_id,
                                       PresenceStatus status,
                                       const std::string& device_id,
                                       const std::string& client_type) {
  std::lock_guard<std::mutex> lock(mu_);

  auto& presence = user_presence_[user_id];
  if (!presence) {
    presence = std::make_shared<PresenceData>();
    presence->user_id = user_id;
    presence->online_since = GetCurrentTimeMs();
  }

  std::lock_guard<std::mutex> presence_lock(presence->mu);

  PresenceStatus old_status = presence->status;
  presence->status = status;
  presence->last_seen = GetCurrentTimeMs();

  if (!device_id.empty()) {
    presence->device_id = device_id;
    presence->device_status[device_id] = status;
  }

  if (!client_type.empty()) {
    presence->client_type = client_type;
  }

  // Compute overall status from all devices
  PresenceStatus overall = ComputeOverallStatus(*presence);
  presence->status = overall;

  // Notify if status changed
  if (old_status != overall) {
    NotifyPresenceChange(user_id, old_status, overall);
  }

  return true;
}

bool PresenceManagerV2::RecordActivity(const std::string& user_id,
                                      UserActivity activity,
                                      const std::string& device_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto& presence = user_presence_[user_id];
  if (!presence) {
    presence = std::make_shared<PresenceData>();
    presence->user_id = user_id;
  }

  std::lock_guard<std::mutex> presence_lock(presence->mu);

  int64_t now = GetCurrentTimeMs();
  presence->last_seen = now;

  PresenceStatus old_status = presence->status;

  // Update status based on activity
  if (activity == UserActivity::IDLE || activity == UserActivity::AWAY) {
    // User is idle - don't change immediately, let timeout handle it
  } else {
    // User is active
    if (presence->status == PresenceStatus::OFFLINE ||
        presence->status == PresenceStatus::IDLE) {
      presence->status = PresenceStatus::ONLINE;
      if (presence->online_since == 0) {
        presence->online_since = now;
      }
    }
  }

  if (!device_id.empty()) {
    presence->device_id = device_id;
    presence->device_status[device_id] = presence->status;
  }

  // Notify if status changed
  if (old_status != presence->status) {
    NotifyPresenceChange(user_id, old_status, presence->status);
  }

  return true;
}

bool PresenceManagerV2::SetCustomStatus(const std::string& user_id,
                                       const std::string& text,
                                       const std::string& emoji,
                                       int64_t duration_ms) {
  std::lock_guard<std::mutex> lock(mu_);

  auto& presence = user_presence_[user_id];
  if (!presence) {
    presence = std::make_shared<PresenceData>();
    presence->user_id = user_id;
  }

  std::lock_guard<std::mutex> presence_lock(presence->mu);

  presence->custom_status.text = text;
  presence->custom_status.emoji = emoji;

  if (duration_ms > 0) {
    presence->custom_status.expires_at = GetCurrentTimeMs() + duration_ms;
  } else {
    presence->custom_status.expires_at = 0;
  }

  return true;
}

bool PresenceManagerV2::ClearCustomStatus(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = user_presence_.find(user_id);
  if (it == user_presence_.end()) {
    return false;
  }

  auto& presence = it->second;
  std::lock_guard<std::mutex> presence_lock(presence->mu);

  presence->custom_status.text.clear();
  presence->custom_status.emoji.clear();
  presence->custom_status.expires_at = 0;

  return true;
}

bool PresenceManagerV2::SetActivity(const std::string& user_id,
                                   const std::string& activity_type,
                                   const std::string& activity_details) {
  std::lock_guard<std::mutex> lock(mu_);

  auto& presence = user_presence_[user_id];
  if (!presence) {
    presence = std::make_shared<PresenceData>();
    presence->user_id = user_id;
  }

  std::lock_guard<std::mutex> presence_lock(presence->mu);

  presence->activity_type = activity_type;
  presence->activity_details = activity_details;
  presence->last_seen = GetCurrentTimeMs();

  // Map activity to status
  if (activity_type == "game") {
    presence->status = PresenceStatus::IN_GAME;
  } else if (activity_type == "voice") {
    presence->status = PresenceStatus::IN_VOICE;
  } else if (activity_type == "call") {
    presence->status = PresenceStatus::IN_CALL;
  }

  return true;
}

bool PresenceManagerV2::GetPresence(const std::string& user_id,
                                    PresenceData* out_data) {
  if (!out_data) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  auto it = user_presence_.find(user_id);
  if (it == user_presence_.end()) {
    // Return offline presence
    out_data->user_id = user_id;
    out_data->status = PresenceStatus::OFFLINE;
    out_data->last_seen = 0;
    return true;
  }

  const auto& presence = it->second;
  std::lock_guard<std::mutex> presence_lock(presence->mu);

  *out_data = *presence;
  return true;
}

std::unordered_map<std::string, PresenceData> PresenceManagerV2::GetPresenceBatch(
    const std::vector<std::string>& user_ids) {

  std::unordered_map<std::string, PresenceData> result;

  std::lock_guard<std::mutex> lock(mu_);

  for (const auto& user_id : user_ids) {
    auto it = user_presence_.find(user_id);
    if (it != user_presence_.end()) {
      const auto& presence = it->second;
      std::lock_guard<std::mutex> presence_lock(presence->mu);
      result[user_id] = *presence;
    } else {
      // Return offline presence
      PresenceData data;
      data.user_id = user_id;
      data.status = PresenceStatus::OFFLINE;
      data.last_seen = 0;
      result[user_id] = data;
    }
  }

  return result;
}

std::vector<std::string> PresenceManagerV2::GetOnlineFriends(
    const std::string& user_id,
    const std::unordered_set<std::string>& friend_ids) {

  std::vector<std::string> result;

  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();
  int64_t cutoff = now - config_.offline_timeout_ms;

  for (const auto& friend_id : friend_ids) {
    auto it = user_presence_.find(friend_id);
    if (it != user_presence_.end()) {
      const auto& presence = it->second;
      std::lock_guard<std::mutex> presence_lock(presence->mu);

      // Include users who are truly online (not idle/offline)
      if (presence->status == PresenceStatus::ONLINE ||
          presence->status == PresenceStatus::IDLE ||
          presence->status == PresenceStatus::DO_NOT_DISTURB ||
          presence->status == PresenceStatus::IN_GAME ||
          presence->status == PresenceStatus::IN_VOICE ||
          presence->status == PresenceStatus::IN_CALL) {

        // Check if actually recent
        if (presence->last_seen >= cutoff) {
          result.push_back(friend_id);
        }
      }
    }
  }

  return result;
}

std::string PresenceManagerV2::SerializePresence(const PresenceData& data) {
  // Serialize to JSON string
  std::ostringstream ss;
  ss << "{"
     << "\"user_id\":\"" << data.user_id << "\","
     << "\"status\":\"" << StatusToString(data.status) << "\","
     << "\"status_message\":\"" << data.status_message << "\","
     << "\"activity_type\":\"" << data.activity_type << "\","
     << "\"activity_details\":\"" << data.activity_details << "\","
     << "\"last_seen\":" << data.last_seen << ","
     << "\"online_since\":" << data.online_since << ","
     << "\"custom_status\":{\"text\":\"" << data.custom_status.text
     << "\",\"emoji\":\"" << data.custom_status.emoji << "\"}"
     << "}";
  return ss.str();
}

bool PresenceManagerV2::DeserializePresence(const std::string& json,
                                          PresenceData* out_data) {
  // In production, use a JSON parser
  // For demo, return false
  return false;
}

bool PresenceManagerV2::RegisterSession(const std::string& user_id,
                                       const std::string& session_id,
                                       const std::string& device_id) {
  std::lock_guard<std::mutex> lock(mu_);

  user_sessions_[user_id].insert(session_id);
  session_to_device_[session_id] = device_id;

  // Mark user as online
  UpdatePresence(user_id, PresenceStatus::ONLINE, device_id);

  return true;
}

bool PresenceManagerV2::UnregisterSession(const std::string& user_id,
                                         const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = user_sessions_.find(user_id);
  if (it == user_sessions_.end()) {
    return false;
  }

  it->second.erase(session_id);
  session_to_device_.erase(session_id);

  // If no more sessions, mark as offline
  if (it->second.empty()) {
    user_sessions_.erase(it);
    UpdatePresence(user_id, PresenceStatus::OFFLINE);
  }

  return true;
}

std::vector<std::string> PresenceManagerV2::GetUserSessions(const std::string& user_id) {
  std::vector<std::string> result;

  std::lock_guard<std::mutex> lock(mu_);

  auto it = user_sessions_.find(user_id);
  if (it == user_sessions_.end()) {
    return result;
  }

  result.insert(result.end(), it->second.begin(), it->second.end());
  return result;
}

void PresenceManagerV2::CleanupIdleUsers() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();
  int64_t idle_cutoff = now - config_.idle_timeout_ms;
  int64_t offline_cutoff = now - config_.offline_timeout_ms;

  for (auto& [user_id, presence] : user_presence_) {
    std::lock_guard<std::mutex> presence_lock(presence->mu);

    if (presence->last_seen < idle_cutoff) {
      PresenceStatus old_status = presence->status;

      if (presence->last_seen < offline_cutoff) {
        presence->status = PresenceStatus::OFFLINE;
      } else {
        presence->status = PresenceStatus::IDLE;
      }

      if (old_status != presence->status) {
        NotifyPresenceChange(user_id, old_status, presence->status);
      }
    }
  }
}

void PresenceManagerV2::CleanupOfflineUsers() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();
  int64_t cutoff = now - (24 * 3600 * 1000);  // 24 hours

  for (auto it = user_presence_.begin(); it != user_presence_.end();) {
    const auto& presence = it->second;
    std::lock_guard<std::mutex> presence_lock(presence->mu);

    // Remove users who have been offline for > 24 hours
    if (presence->status == PresenceStatus::OFFLINE &&
        presence->last_seen > 0 &&
        presence->last_seen < cutoff) {
      it = user_presence_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t PresenceManagerV2::GetOnlineUserCount() const {
  std::lock_guard<std::mutex> lock(mu_);

  size_t count = 0;
  int64_t cutoff = GetCurrentTimeMs() - config_.offline_timeout_ms;

  for (const auto& [user_id, presence] : user_presence_) {
    std::lock_guard<std::mutex> presence_lock(presence->mu);

    if (presence->status != PresenceStatus::OFFLINE &&
        presence->status != PresenceStatus::INVISIBLE &&
        presence->last_seen >= cutoff) {
      ++count;
    }
  }

  return count;
}

size_t PresenceManagerV2::GetTotalSessionCount() const {
  std::lock_guard<std::mutex> lock(mu_);

  size_t count = 0;
  for (const auto& [user_id, sessions] : user_sessions_) {
    count += sessions.size();
  }

  return count;
}

PresenceStatus PresenceManagerV2::ComputeOverallStatus(const PresenceData& data) {
  // Determine overall status from device statuses
  if (data.device_status.empty()) {
    return data.status;
  }

  // Priority: DND > In Voice/Call/In Game > Online > Idle
  bool has_online = false;
  bool has_idle = false;
  bool has_dnd = false;
  bool has_voice = false;
  bool has_game = false;

  for (const auto& [device, status] : data.device_status) {
    switch (status) {
      case PresenceStatus::DO_NOT_DISTURB:
        has_dnd = true;
        break;
      case PresenceStatus::IN_VOICE:
      case PresenceStatus::IN_CALL:
        has_voice = true;
        break;
      case PresenceStatus::IN_GAME:
        has_game = true;
        break;
      case PresenceStatus::ONLINE:
        has_online = true;
        break;
      case PresenceStatus::IDLE:
        has_idle = true;
        break;
      default:
        break;
    }
  }

  if (has_dnd) return PresenceStatus::DO_NOT_DISTURB;
  if (has_voice) return PresenceStatus::IN_VOICE;
  if (has_game) return PresenceStatus::IN_GAME;
  if (has_online) return PresenceStatus::ONLINE;
  if (has_idle) return PresenceStatus::IDLE;

  return PresenceStatus::OFFLINE;
}

void PresenceManagerV2::NotifyPresenceChange(const std::string& user_id,
                                            PresenceStatus old_status,
                                            PresenceStatus new_status) {
  if (presence_change_callback_) {
    presence_change_callback_(user_id, old_status, new_status);
  }
}

} // namespace social
} // namespace chirp
