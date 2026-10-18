#include "inject_idempotency.h"

#include <cstdint>

#include "runtime_utils.h"

namespace chirp::chat {

TtlInjectIdempotency::TtlInjectIdempotency(int64_t ttl_ms, std::size_t max_entries)
    : ttl_ms_(ttl_ms < 0 ? 0 : ttl_ms),
      max_entries_(max_entries == 0 ? 1 : max_entries),
      now_([this] { return chirp::chat::runtime::NowMs(); }) {}

void TtlInjectIdempotency::SetNow(std::function<int64_t()> now) {
  now_ = std::move(now);
}

int64_t TtlInjectIdempotency::Now() const { return now_(); }

void TtlInjectIdempotency::PruneExpired() {
  const int64_t now = Now();
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.expires_at_ms <= now) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

std::optional<std::string> TtlInjectIdempotency::Lookup(
    const std::string& inject_id) {
  const auto it = entries_.find(inject_id);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  if (it->second.expires_at_ms <= Now()) {
    entries_.erase(it);
    return std::nullopt;
  }
  return it->second.message_id;
}

void TtlInjectIdempotency::Remember(const std::string& inject_id,
                                    const std::string& message_id) {
  PruneExpired();
  if (entries_.find(inject_id) == entries_.end() &&
      entries_.size() >= max_entries_) {
    // No cheaper eviction order is available without a separate index; the
    // entry closest to expiring is the least likely to be replayed.
    std::string drop_key;
    int64_t earliest = INT64_MAX;
    for (const auto& entry : entries_) {
      if (entry.second.expires_at_ms < earliest) {
        earliest = entry.second.expires_at_ms;
        drop_key = entry.first;
      }
    }
    entries_.erase(drop_key);
  }
  entries_[inject_id] = Entry{message_id, Now() + ttl_ms_};
}

}  // namespace chirp::chat
