#ifndef CHIRP_CHAT_INJECT_IDEMPOTENCY_H_
#define CHIRP_CHAT_INJECT_IDEMPOTENCY_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace chirp::chat {

// Remembers which inject_id already minted a message, so a hub redelivery or
// a short retry storm on the same INJECT_MESSAGE_NOTIFY reuses the stored
// message instead of appending a second copy to history.
//
// game_server_gateway.proto advertises inject_id as an idempotency key, but the
// consumer used to ignore it and minted a fresh message per notify - a single
// lost chat->hub ack produced a duplicate line in history.
//
// Single-threaded contract: everything is invoked on the chat io thread (the
// same thread the InjectHooks run on), so implementations need no locking.
class InjectIdempotency {
 public:
  virtual ~InjectIdempotency() = default;
  // Message id minted for this inject_id, if one exists and has not expired.
  virtual std::optional<std::string> Lookup(const std::string& inject_id) = 0;
  // Remember that inject_id produced message_id.
  virtual void Remember(const std::string& inject_id, const std::string& message_id) = 0;
};

// In-process TTL record. It covers hub redelivery inside one chat process, and
// it deliberately does not: it survives no restart and it is not shared between
// chat instances, so a multi-instance chat plane still needs a shared store for
// true idempotency.
class TtlInjectIdempotency : public InjectIdempotency {
 public:
  // ttl_ms: how long an inject_id stays usable as a dedup key.
  // max_entries: hard cap; entries closest to expiring drop out first so the
  // map cannot grow without bound on a chatty hub.
  TtlInjectIdempotency(int64_t ttl_ms, std::size_t max_entries = kDefaultMaxEntries);

  std::optional<std::string> Lookup(const std::string& inject_id) override;
  void Remember(const std::string& inject_id, const std::string& message_id) override;

  // Injectable clock for tests that need to fast-forward past the TTL.
  void SetNow(std::function<int64_t()> now);

  std::size_t Size() const { return entries_.size(); }

  static constexpr int64_t kDefaultTtlMs = 5 * 60 * 1000;  // 5 minutes
  static constexpr std::size_t kDefaultMaxEntries = 10000;

 private:
  int64_t Now() const;
  void PruneExpired();

  struct Entry {
    std::string message_id;
    int64_t expires_at_ms;
  };

  std::map<std::string, Entry> entries_;
  int64_t ttl_ms_;
  std::size_t max_entries_;
  std::function<int64_t()> now_;
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_INJECT_IDEMPOTENCY_H_
