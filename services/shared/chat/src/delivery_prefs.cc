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

} // namespace chirp::chat
