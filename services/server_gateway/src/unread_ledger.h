#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "network/redis_client.h"
#include "proto/server_gateway.pb.h"

namespace chirp::server_gateway {

// Unified unread ledger (WP-8 slice 4): per-player badge counters for
// subscription channels — "how many fan-in notifications has this player not
// handled on (game, channel) yet". The fan-in path (FanoutInject) increments
// once per copy successfully handed to the chat service; the MarkChannelsRead
// / GetUnreadSummary RPCs clear and read the counters back. This is a badge,
// not a read cursor: it never sees chat-side reads and unsubscribing does
// not clear it (marking read is the only decrementing path).
//
// In-memory maps are authoritative; an optional Redis client write-through
// (one serialized StoredUnreadEntry per counter under
// chirp:unread:entry:<player>:<game>:<channel>) keeps counters across hub
// restarts, loaded by Load() before the hub starts serving. Redis failures
// degrade to memory-only operation with a warning, like every other
// write-through in this codebase. The key concatenates raw id components,
// so ids containing ':' can alias another entry's key on disk; the in-memory
// map keeps the exact tuple and Load parses records (not key names), so this
// only limits restart fidelity for exotic ids. All methods are thread-safe
// under one mutex.
class UnreadLedger {
 public:
  // Returns nullptr (or an empty factory) for memory-only operation.
  using RedisFactory = std::function<std::unique_ptr<network::RedisClient>()>;

  UnreadLedger() = default;
  explicit UnreadLedger(RedisFactory factory);

  // Pulls persisted counters into memory. Redis-unavailable is not fatal:
  // the ledger logs and keeps running memory-only (entries written after
  // this point still try the write-through).
  void Load();

  // Adds one unhandled notification for (player, game, channel). Empty
  // components are a defensive no-op — the fan-in path validates first.
  void Increment(const std::string& player_id, const std::string& game_id,
                 const std::string& channel_id);

  // Layered selector, mirroring MarkChannelsReadRequest: `channel_id` set
  // (with `game_id`) clears that one channel; only `game_id` clears every
  // channel of that game; both empty clears everything the player has.
  // Unknown targets are a no-op (marking read is idempotent). Returns the
  // number of entries removed.
  size_t MarkRead(const std::string& player_id, const std::string& game_id,
                  const std::string& channel_id);

  // The player's nonzero counters, ordered by (game_id, channel_id),
  // optionally restricted to one game.
  std::vector<UnreadSummaryEntry> GetSummary(const std::string& player_id,
                                             const std::string& game_id) const;

  size_t Size() const;

 private:
  // Per-player map keeps (game, channel) sorted (std::map) so summaries
  // come out in a stable order; the outer per-player map is keyed for
  // lookup only.
  using ChannelCounters = std::map<std::pair<std::string, std::string>, int32_t>;

  void PersistLocked(const std::string& player_id, const std::string& game_id,
                     const std::string& channel_id, int32_t count);
  void PersistDeleteLocked(const std::string& player_id, const std::string& game_id,
                           const std::string& channel_id);

  RedisFactory redis_factory_;
  std::unique_ptr<network::RedisClient> redis_;  // nullptr = memory-only

  mutable std::mutex mu_;
  std::unordered_map<std::string, ChannelCounters> entries_;
};

}  // namespace chirp::server_gateway
