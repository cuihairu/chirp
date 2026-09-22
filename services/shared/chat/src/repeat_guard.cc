#include "repeat_guard.h"

namespace chirp::chat {

bool RepeatGuard::Allow(const std::string& user_id, const std::string& content, int64_t now_ms) {
  RepeatState& state = states_[user_id];
  if (now_ms < state.muted_until_ms) {
    return false;
  }
  if (state.muted_until_ms != 0) {
    // The mute just expired: start from a clean slate.
    state = RepeatState{};
  }
  if (state.count > 0 && state.last_content == content) {
    ++state.count;
  } else {
    state.last_content = content;
    state.count = 1;
  }
  if (state.count >= kRepeatTriggerCount) {
    state.muted_until_ms = now_ms + kRepeatMuteMs;
    state.count = 0;
    state.last_content.clear();
    return false;
  }
  return true;
}

} // namespace chirp::chat
