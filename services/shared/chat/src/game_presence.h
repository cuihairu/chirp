#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "network/redis_client.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp::chat {

// Per-player game-presence switch (游戏在线状态). The default is ENABLED:
// binding a game identity opts the player in (the product decision is
// recorded in docs/server_plane.md), and only an explicit choice writes a
// row. The registry stores just that override — everything else (which
// games are live, the derived Redis roster, the pub/sub events) is computed
// by PlayerDirectory from the identity bindings.
//
// Redis write-through under chirp:game_presence:setting:<player_id>
// ("1"/"0"), replayed by Load() before the hub starts serving. Like every
// other write-through in this codebase, Redis is durability only: the
// in-memory map is the working authority and a failed write degrades to
// memory-only operation. All methods are thread-safe under one mutex.
class GamePresence {
 public:
  // Returns nullptr (or an empty factory) for memory-only operation.
  using RedisFactory = std::function<std::unique_ptr<chirp::network::RedisClient>()>;

  explicit GamePresence(RedisFactory factory);

  // Replays persisted explicit choices. Redis-unavailable is not fatal: the
  // registry logs and keeps running memory-only.
  void Load();

  // True unless the player explicitly disabled the switch. Unknown players
  // read as enabled — the default-after-bind semantics live here.
  bool Enabled(const std::string& player_id) const;

  // Stores the explicit choice (idempotent). Returns whether this call
  // changed the effective value — callers use it to decide whether the
  // derived presence roster needs refreshing.
  bool SetEnabled(const std::string& player_id, bool enabled);

 private:
  RedisFactory redis_factory_;
  std::unique_ptr<chirp::network::RedisClient> redis_;  // nullptr = memory-only

  mutable std::mutex mu_;
  // Only explicit choices live here; absence means "enabled".
  std::unordered_map<std::string, bool> overrides_;
};

}  // namespace chirp::chat
