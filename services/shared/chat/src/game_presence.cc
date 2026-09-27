#include "game_presence.h"

#include <string_view>

#include "logger.h"

namespace chirp::chat {
namespace {
constexpr std::string_view kSettingPrefix = "chirp:game_presence:setting:";
}  // namespace

GamePresence::GamePresence(RedisFactory factory)
    : redis_factory_(std::move(factory)),
      redis_(redis_factory_ ? redis_factory_() : nullptr) {}

void GamePresence::Load() {
  if (!redis_) {
    return;
  }
  const auto keys = redis_->Keys(std::string(kSettingPrefix) + "*");
  size_t loaded = 0;
  for (const auto& key : keys) {
    const auto value = redis_->Get(key);
    if (!value || (value != "0" && value != "1")) {
      chirp::common::Logger::Instance().Warn(
          "game presence: skipping unreadable setting " + key);
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      overrides_[key.substr(kSettingPrefix.size())] = value == "1";
    }
    ++loaded;
  }
  if (loaded > 0) {
    chirp::common::Logger::Instance().Info(
        "game presence: replayed " + std::to_string(loaded) + " explicit setting(s)");
  }
}

bool GamePresence::Enabled(const std::string& player_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = overrides_.find(player_id);
  return it == overrides_.end() || it->second;
}

bool GamePresence::SetEnabled(const std::string& player_id, bool enabled) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = overrides_.find(player_id);
    changed = it == overrides_.end() || it->second != enabled;
    overrides_[player_id] = enabled;
  }
  // Durability is best-effort: the memory write above already took effect.
  if (redis_ && !redis_->Set(std::string(kSettingPrefix) + player_id, enabled ? "1" : "0")) {
    chirp::common::Logger::Instance().Warn(
        "game presence: failed to persist setting for " + player_id);
  }
  return changed;
}

}  // namespace chirp::chat
