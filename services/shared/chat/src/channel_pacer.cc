#include "channel_pacer.h"

namespace chirp::chat {

int64_t MinSendIntervalMs(ChannelType type) {
  switch (type) {
    case WORLD:
      return 5000;
    case GUILD:
      return 2000;
    case PRIVATE:
      return 1000;
    default:
      return 0;
  }
}

bool ChannelPacer::Allow(const std::string& user_id, ChannelType type, int64_t now_ms) {
  const int64_t interval = MinSendIntervalMs(type);
  if (interval == 0) {
    return true;
  }
  // \x1f cannot appear in a protobuf string id, so the pair is unambiguous.
  const std::string key = user_id + '\x1f' + std::to_string(static_cast<int>(type));
  const auto it = last_send_ms_.find(key);
  if (it != last_send_ms_.end() && now_ms - it->second < interval) {
    return false;
  }
  last_send_ms_[key] = now_ms;
  return true;
}

} // namespace chirp::chat
