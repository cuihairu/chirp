#include "unread_ledger.h"

#include "common/logger.h"

namespace chirp::server_gateway {

namespace {

constexpr const char* kEntryPrefix = "chirp:unread:entry:";

std::string EntryKey(const std::string& player_id, const std::string& game_id,
                     const std::string& channel_id) {
  return kEntryPrefix + player_id + ":" + game_id + ":" + channel_id;
}

}  // namespace

UnreadLedger::UnreadLedger(RedisFactory factory) : redis_factory_(std::move(factory)) {
  if (redis_factory_) {
    redis_ = redis_factory_();
  }
}

void UnreadLedger::Load() {
  if (!redis_) {
    return;
  }
  size_t loaded = 0;
  for (const auto& key : redis_->Keys(kEntryPrefix + std::string("*"))) {
    const auto value = redis_->Get(key);
    if (!value) {
      continue;
    }
    StoredUnreadEntry entry;
    if (!entry.ParseFromString(*value)) {
      chirp::common::Logger::Instance().Warn(
          "unread ledger store: skipping unparseable entry " + key);
      continue;
    }
    if (entry.unread_count() <= 0) {
      // Cleared entries are deleted from Redis, never written as zero; a
      // zero (or negative) record would only confuse the summary.
      continue;
    }
    entries_[entry.player_id()][{entry.game_id(), entry.channel_id()}] = entry.unread_count();
    ++loaded;
  }
  chirp::common::Logger::Instance().Info(
      "unread ledger store: loaded " + std::to_string(loaded) + " entries");
}

void UnreadLedger::Increment(const std::string& player_id, const std::string& game_id,
                             const std::string& channel_id) {
  if (player_id.empty() || game_id.empty() || channel_id.empty()) {
    return;  // defensive: the fan-in path validates before calling
  }
  std::lock_guard<std::mutex> lock(mu_);
  const int32_t count = ++entries_[player_id][{game_id, channel_id}];
  PersistLocked(player_id, game_id, channel_id, count);
}

size_t UnreadLedger::MarkRead(const std::string& player_id, const std::string& game_id,
                              const std::string& channel_id) {
  if (player_id.empty()) {
    return 0;  // defensive: the handler validates before calling
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto pit = entries_.find(player_id);
  if (pit == entries_.end()) {
    return 0;
  }
  size_t cleared = 0;
  if (!channel_id.empty()) {
    const auto cit = pit->second.find({game_id, channel_id});
    if (cit == pit->second.end()) {
      return 0;
    }
    PersistDeleteLocked(player_id, game_id, channel_id);
    pit->second.erase(cit);
    cleared = 1;
  } else if (!game_id.empty()) {
    for (auto cit = pit->second.begin(); cit != pit->second.end();) {
      if (cit->first.first == game_id) {
        PersistDeleteLocked(player_id, cit->first.first, cit->first.second);
        cit = pit->second.erase(cit);
        ++cleared;
      } else {
        ++cit;
      }
    }
  } else {
    for (const auto& [key, count] : pit->second) {
      PersistDeleteLocked(player_id, key.first, key.second);
      ++cleared;
    }
    pit->second.clear();
  }
  if (pit->second.empty()) {
    entries_.erase(pit);
  }
  return cleared;
}

std::vector<UnreadSummaryEntry> UnreadLedger::GetSummary(const std::string& player_id,
                                                         const std::string& game_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<UnreadSummaryEntry> out;
  const auto pit = entries_.find(player_id);
  if (pit == entries_.end()) {
    return out;
  }
  for (const auto& [key, count] : pit->second) {
    if (!game_id.empty() && key.first != game_id) {
      continue;
    }
    UnreadSummaryEntry entry;
    entry.set_game_id(key.first);
    entry.set_channel_id(key.second);
    entry.set_unread_count(count);
    out.push_back(std::move(entry));
  }
  return out;
}

size_t UnreadLedger::Size() const {
  std::lock_guard<std::mutex> lock(mu_);
  size_t total = 0;
  for (const auto& [player, counters] : entries_) {
    total += counters.size();
  }
  return total;
}

void UnreadLedger::PersistLocked(const std::string& player_id, const std::string& game_id,
                                 const std::string& channel_id, int32_t count) {
  if (!redis_) {
    return;
  }
  StoredUnreadEntry entry;
  entry.set_player_id(player_id);
  entry.set_game_id(game_id);
  entry.set_channel_id(channel_id);
  entry.set_unread_count(count);
  if (!redis_->Set(EntryKey(player_id, game_id, channel_id), entry.SerializeAsString())) {
    // Write-through is best-effort: memory stays authoritative and the next
    // increment of the same counter retries the write.
    chirp::common::Logger::Instance().Warn(
        "unread ledger store: redis write failed for " + player_id + ":" + game_id + ":" +
        channel_id);
  }
}

void UnreadLedger::PersistDeleteLocked(const std::string& player_id, const std::string& game_id,
                                       const std::string& channel_id) {
  if (!redis_) {
    return;
  }
  if (!redis_->Del(EntryKey(player_id, game_id, channel_id))) {
    chirp::common::Logger::Instance().Warn(
        "unread ledger store: redis delete failed for " + player_id + ":" + game_id + ":" +
        channel_id);
  }
}

}  // namespace chirp::server_gateway
