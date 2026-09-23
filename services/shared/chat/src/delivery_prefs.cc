#include "delivery_prefs.h"

namespace chirp::chat {

bool IsMuteableChannel(ChannelType type) {
  for (const ChannelType muteable : kMuteableChannels) {
    if (type == muteable) {
      return true;
    }
  }
  return false;
}

int DeliveryPrefs::SlotFor(ChannelType type) {
  for (int i = 0; i < 3; i++) {
    if (type == kMuteableChannels[i]) {
      return i;
    }
  }
  return -1;
}

bool DeliveryPrefs::SetChannelMuted(const std::string& user_id, ChannelType type,
                                    bool muted) {
  const int slot = SlotFor(type);
  if (slot < 0) {
    return false;
  }
  return mutes_[user_id][slot] = muted;
}

bool DeliveryPrefs::IsChannelMuted(const std::string& user_id, ChannelType type) const {
  const int slot = SlotFor(type);
  if (slot < 0) {
    return false;
  }
  const auto it = mutes_.find(user_id);
  return it != mutes_.end() && it->second[slot];
}

std::vector<std::pair<ChannelType, bool>> DeliveryPrefs::GetChannelMutes(
    const std::string& user_id) const {
  std::vector<std::pair<ChannelType, bool>> result;
  result.reserve(3);
  const auto it = mutes_.find(user_id);
  for (int i = 0; i < 3; i++) {
    result.emplace_back(kMuteableChannels[i],
                        it != mutes_.end() ? it->second[i] : false);
  }
  return result;
}

bool DeliveryPrefs::BlockUser(const std::string& user_id,
                              const std::string& target_id) {
  if (target_id.empty() || target_id == user_id) {
    return false;
  }
  blocked_[user_id].insert(target_id);
  return true;
}

bool DeliveryPrefs::UnblockUser(const std::string& user_id,
                                const std::string& target_id) {
  if (target_id.empty()) {
    return false;
  }
  blocked_[user_id].erase(target_id);
  return true;
}

bool DeliveryPrefs::IsUserBlocked(const std::string& user_id,
                                  const std::string& target_id) const {
  const auto it = blocked_.find(user_id);
  return it != blocked_.end() && it->second.count(target_id) > 0;
}

std::vector<std::string> DeliveryPrefs::GetBlockedUsers(
    const std::string& user_id) const {
  const auto it = blocked_.find(user_id);
  if (it == blocked_.end()) {
    return {};
  }
  return {it->second.begin(), it->second.end()};
}

} // namespace chirp::chat
