#include "typing_manager.h"

namespace chirp {
namespace chat {

TypingManager::TypingManager(const TypingConfig& config)
    : config_(config) {}

std::string TypingManager::GetChannelKey(const std::string& channel_id,
                                        ChannelType channel_type) const {
  return std::to_string(static_cast<int>(channel_type)) + ":" + channel_id;
}

int64_t TypingManager::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

bool TypingManager::UserStartedTyping(const std::string& channel_id,
                                     ChannelType channel_type,
                                     const std::string& user_id,
                                     const std::string& username,
                                     TypingIndicator* out_notify) {
  std::lock_guard<std::mutex> lock(mu_);

  std::string key = GetChannelKey(channel_id, channel_type);
  auto& channel_users = channel_typing_[key];

  int64_t now = GetCurrentTimeMs();
  auto it = channel_users.find(user_id);

  bool should_notify = false;

  if (it == channel_users.end()) {
    // New user typing - always notify
    TypingUser user;
    user.user_id = user_id;
    user.username = username;
    user.last_typing_time = now;
    user.last_broadcast_time = now - config_.cooldown_ms - 1;  // Force broadcast
    channel_users[user_id] = user;
    should_notify = true;
  } else {
    // Update existing user
    it->second.last_typing_time = now;
    it->second.username = username;  // Update username in case it changed

    // Only notify if cooldown has passed
    if (now - it->second.last_broadcast_time >= config_.cooldown_ms) {
      it->second.last_broadcast_time = now;
      should_notify = true;
    }
  }

  // Build output if requested
  if (out_notify && should_notify) {
    out_notify->set_channel_id(channel_id);
    out_notify->set_channel_type(channel_type);
    out_notify->set_user_id(user_id);
    out_notify->set_username(username);
    out_notify->set_is_typing(true);
    out_notify->set_timestamp(now);
  }

  return should_notify;
}

bool TypingManager::UserStoppedTyping(const std::string& channel_id,
                                     ChannelType channel_type,
                                     const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  std::string key = GetChannelKey(channel_id, channel_type);
  auto it = channel_typing_.find(key);

  if (it == channel_typing_.end()) {
    return false;
  }

  auto user_it = it->second.find(user_id);
  if (user_it == it->second.end()) {
    return false;
  }

  // Remove user from typing list
  it->second.erase(user_it);

  // Clean up empty channels
  if (it->second.empty()) {
    channel_typing_.erase(it);
  }

  return true;
}

std::vector<std::string> TypingManager::GetTypingUsers(
    const std::string& channel_id,
    ChannelType channel_type) {

  std::vector<std::string> result;

  std::lock_guard<std::mutex> lock(mu_);

  std::string key = GetChannelKey(channel_id, channel_type);
  auto it = channel_typing_.find(key);

  if (it == channel_typing_.end()) {
    return result;
  }

  int64_t now = GetCurrentTimeMs();
  int64_t cutoff = now - config_.typing_timeout_ms;

  result.reserve(it->second.size());

  for (auto& [user_id, user] : it->second) {
    // Skip expired users
    if (user.last_typing_time >= cutoff) {
      result.push_back(user_id);
    }
  }

  // Limit result
  if (result.size() > static_cast<size_t>(config_.max_typing_users)) {
    result.resize(config_.max_typing_users);
  }

  return result;
}

bool TypingManager::GetTypingIndicator(const std::string& channel_id,
                                      ChannelType channel_type,
                                      const std::string& user_id,
                                      TypingIndicator* out_indicator) {
  if (!out_indicator) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  std::string key = GetChannelKey(channel_id, channel_type);
  auto it = channel_typing_.find(key);

  if (it == channel_typing_.end()) {
    return false;
  }

  auto user_it = it->second.find(user_id);
  if (user_it == it->second.end()) {
    return false;
  }

  out_indicator->set_channel_id(channel_id);
  out_indicator->set_channel_type(channel_type);
  out_indicator->set_user_id(user_id);
  out_indicator->set_username(user_it->second.username);
  out_indicator->set_is_typing(true);
  out_indicator->set_timestamp(user_it->second.last_typing_time);

  return true;
}

void TypingManager::CleanupExpired() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();
  int64_t cutoff = now - config_.typing_timeout_ms;

  for (auto it = channel_typing_.begin(); it != channel_typing_.end();) {
    // Remove expired users from this channel
    for (auto user_it = it->second.begin(); user_it != it->second.end();) {
      if (user_it->second.last_typing_time < cutoff) {
        user_it = it->second.erase(user_it);
      } else {
        ++user_it;
      }
    }

    // Remove empty channels
    if (it->second.empty()) {
      it = channel_typing_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t TypingManager::GetActiveChannelCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return channel_typing_.size();
}

size_t TypingManager::GetTotalTypingUserCount() const {
  std::lock_guard<std::mutex> lock(mu_);

  size_t total = 0;
  for (const auto& [key, users] : channel_typing_) {
    total += users.size();
  }

  return total;
}

} // namespace chat
} // namespace chirp
