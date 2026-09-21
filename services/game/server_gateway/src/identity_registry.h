#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "network/redis_client.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp::game_server_gateway {

// Player identity bindings (WP-8 slice 1): the game backend's assertion
// "platform player P is game user U of game G". The binding_id is the
// caller's idempotency key; (game_id, game_user_id) is the unique index —
// re-asserting a game user under a new binding_id overwrites (the backend
// is the authority on where a game user belongs).
//
// In-memory maps are authoritative; an optional Redis client write-through
// (one serialized StoredIdentityBinding per binding under
// chirp:binding:entry:<binding_id>) keeps bindings across hub restarts,
// loaded by Load() before the hub starts serving. Redis failures degrade to
// memory-only operation with a warning, like every other write-through in
// this codebase. All methods are thread-safe under one mutex.
class IdentityRegistry {
 public:
  // Returns nullptr (or an empty factory) for memory-only operation.
  using RedisFactory = std::function<std::unique_ptr<network::RedisClient>()>;

  IdentityRegistry() = default;
  explicit IdentityRegistry(RedisFactory factory);

  // Pulls persisted bindings into memory. Redis-unavailable is not fatal:
  // the registry logs and keeps running memory-only (bindings written after
  // this point still try the write-through).
  void Load();

  enum class BindOutcome {
    kBound,    // newly stored
    kExisted,  // identical binding already stored (idempotent no-op)
    kInvalid,  // same binding_id with a different tuple
  };

  // `bound_at_ms` is stamped by the caller (the handler owns the clock).
  BindOutcome Bind(const std::string& binding_id, const std::string& player_id,
                   const std::string& game_id, const std::string& game_user_id,
                   int64_t bound_at_ms);

  // Removes by binding_id or by (game_id, game_user_id); unknown targets
  // are a no-op (unbinding is idempotent, like event acks). Returns whether
  // a binding was actually removed.
  bool UnbindById(const std::string& binding_id);
  bool UnbindByGameUser(const std::string& game_id, const std::string& game_user_id);

  // All bindings asserted for one player (one per game identity).
  std::vector<StoredIdentityBinding> GetByPlayer(const std::string& player_id) const;

  // The platform player behind a game user, or nullptr when unbound.
  std::unique_ptr<std::string> Resolve(const std::string& game_id,
                                       const std::string& game_user_id) const;

  size_t Size() const;

 private:
  void EraseEntryLocked(const StoredIdentityBinding& entry);
  void PersistLocked(const StoredIdentityBinding& entry);
  void PersistDeleteLocked(const std::string& binding_id);

  std::vector<StoredIdentityBinding> LoadFromRedis();

  RedisFactory redis_factory_;
  std::unique_ptr<network::RedisClient> redis_;  // nullptr = memory-only

  mutable std::mutex mu_;
  std::unordered_map<std::string, StoredIdentityBinding> by_id_;
  // Unique index: game user -> binding_id.
  std::map<std::pair<std::string, std::string>, std::string> game_user_index_;
  // Forward index: player -> binding ids.
  std::unordered_map<std::string, std::unordered_set<std::string>> player_index_;
};

}  // namespace chirp::game_server_gateway
