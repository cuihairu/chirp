#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace chirp::chat {

// Anti-flood mute window (game_chat_features P0 重复消息检测): the third
// consecutive identical send mutes the user for five minutes.
inline constexpr int kRepeatTriggerCount = 3;
inline constexpr int64_t kRepeatMuteMs = 5 * 60 * 1000;

// Per-user consecutive-identical-content tracking. Handlers run on the
// connection's io thread, so the state is touched from that thread only.
class RepeatGuard {
 public:
  // True = the send may proceed. False = refused: either the user is inside
  // a mute window, or this send is the kRepeatTriggerCount-th identical one
  // that starts the window. While muted every content is refused, not just
  // the repeated one.
  bool Allow(const std::string& user_id, const std::string& content, int64_t now_ms);

 private:
  struct RepeatState {
    std::string last_content;
    int count = 0;
    int64_t muted_until_ms = 0;
  };
  std::unordered_map<std::string, RepeatState> states_;
};

} // namespace chirp::chat
