#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "network/redis_client.h"
#include "proto/server_gateway.pb.h"

namespace chirp::server_gateway {

// Player channel subscriptions (WP-8 slice 2): the recorded intent "platform
// player P follows channel C of game G". Game backends assert subscriptions
// over the trusted server plane carrying a caller-supplied subscription_id
// (the idempotency key); the app edge forwards self-served players with an
// empty id and the server mints one. The (player_id, game_id, channel_id)
// tuple is the unique index. This registry stores intent only — no delivery
// happens here (fan-in routing is a later slice).
//
// In-memory maps are authoritative; an optional Redis client write-through
// (one serialized StoredChannelSubscription per subscription under
// chirp:subscription:entry:<subscription_id>) keeps subscriptions across hub
// restarts, loaded by Load() before the hub starts serving. Redis failures
// degrade to memory-only operation with a warning, like every other
// write-through in this codebase. All methods are thread-safe under one
// mutex.
class SubscriptionRegistry {
 public:
  // Returns nullptr (or an empty factory) for memory-only operation.
  using RedisFactory = std::function<std::unique_ptr<network::RedisClient>()>;

  SubscriptionRegistry() = default;
  explicit SubscriptionRegistry(RedisFactory factory);

  // Pulls persisted subscriptions into memory. Redis-unavailable is not
  // fatal: the registry logs and keeps running memory-only (subscriptions
  // written after this point still try the write-through).
  void Load();

  enum class SubscribeOutcome {
    kSubscribed,  // newly stored (or the tuple moved under a new id)
    kExisted,     // identical subscription already stored (idempotent no-op)
    kInvalid,     // same subscription_id with a different tuple
  };

  // An empty `subscription_id` mints one. Re-subscribing an existing tuple
  // with a different id replaces the old record (the backend is the
  // authority); re-subscribing with an empty id is an idempotent no-op that
  // keeps the stored id stable. `subscribed_at_ms` is stamped by the caller
  // (the handler owns the clock).
  SubscribeOutcome Subscribe(std::string* subscription_id, const std::string& player_id,
                             const std::string& game_id, const std::string& channel_id,
                             int64_t subscribed_at_ms);

  // Removes by subscription_id or by the full tuple; unknown targets are a
  // no-op (unsubscribing is idempotent, like event acks). Returns whether a
  // subscription was actually removed.
  bool UnsubscribeById(const std::string& subscription_id);
  bool UnsubscribeByTuple(const std::string& player_id, const std::string& game_id,
                          const std::string& channel_id);

  // All subscriptions of one player, optionally restricted to one game.
  std::vector<StoredChannelSubscription> GetForPlayer(const std::string& player_id,
                                                      const std::string& game_id) const;

  size_t Size() const;

 private:
  void EraseEntryLocked(const StoredChannelSubscription& entry);
  void PersistLocked(const StoredChannelSubscription& entry);
  void PersistDeleteLocked(const std::string& subscription_id);

  std::vector<StoredChannelSubscription> LoadFromRedis();

  RedisFactory redis_factory_;
  std::unique_ptr<network::RedisClient> redis_;  // nullptr = memory-only

  mutable std::mutex mu_;
  std::unordered_map<std::string, StoredChannelSubscription> by_id_;
  // Unique index: (player, game, channel) -> subscription_id.
  std::map<std::tuple<std::string, std::string, std::string>, std::string> tuple_index_;
  // Forward index: player -> subscription ids.
  std::unordered_map<std::string, std::unordered_set<std::string>> player_index_;
};

}  // namespace chirp::server_gateway
