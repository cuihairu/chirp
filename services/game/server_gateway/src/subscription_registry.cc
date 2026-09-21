#include "subscription_registry.h"

#include <atomic>
#include <random>
#include <sstream>

#include "common/logger.h"

namespace chirp::game_server_gateway {

namespace {

constexpr const char* kEntryPrefix = "chirp:subscription:entry:";

// Minted ids only need to be unique within what the registry stores: a
// random per-process salt plus a counter can never collide with itself, and
// a collision with a caller-chosen id just means the tuple checks decide.
std::string MintSubscriptionId() {
  static std::atomic<uint64_t> counter{0};
  static const uint64_t salt = std::random_device{}();
  std::ostringstream oss;
  oss << "sub-" << std::hex << salt << "-" << counter.fetch_add(1);
  return oss.str();
}

std::tuple<std::string, std::string, std::string> TupleKey(const std::string& player_id,
                                                           const std::string& game_id,
                                                           const std::string& channel_id) {
  return std::make_tuple(player_id, game_id, channel_id);
}

}  // namespace

SubscriptionRegistry::SubscriptionRegistry(RedisFactory factory)
    : redis_factory_(std::move(factory)) {
  if (redis_factory_) {
    redis_ = redis_factory_();
  }
}

std::vector<StoredChannelSubscription> SubscriptionRegistry::LoadFromRedis() {
  if (!redis_) {
    return {};
  }
  const auto keys = redis_->Keys(kEntryPrefix + std::string("*"));
  std::vector<StoredChannelSubscription> loaded;
  loaded.reserve(keys.size());
  for (const auto& key : keys) {
    const auto value = redis_->Get(key);
    if (!value) {
      continue;
    }
    StoredChannelSubscription entry;
    if (!entry.ParseFromString(*value)) {
      chirp::common::Logger::Instance().Warn(
          "player channel subscription store: skipping unparseable entry " + key);
      continue;
    }
    loaded.push_back(std::move(entry));
  }
  return loaded;
}

void SubscriptionRegistry::Load() {
  const auto loaded = LoadFromRedis();
  if (loaded.empty() && !redis_) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& entry : loaded) {
    // Entries persisted under the same unique tuple replace each other
    // exactly as a Subscribe would; load order then only matters for ties,
    // which the backend never creates (subscription ids are unique).
    const auto key = TupleKey(entry.player_id(), entry.game_id(), entry.channel_id());
    const auto clash = tuple_index_.find(key);
    if (clash != tuple_index_.end()) {
      const auto stale = by_id_.find(clash->second);
      if (stale != by_id_.end()) {
        EraseEntryLocked(stale->second);
      }
    }
    tuple_index_[key] = entry.subscription_id();
    player_index_[entry.player_id()].insert(entry.subscription_id());
    channel_index_[{entry.game_id(), entry.channel_id()}].insert(entry.subscription_id());
    by_id_[entry.subscription_id()] = entry;
  }
  chirp::common::Logger::Instance().Info(
      "player channel subscription store: loaded " + std::to_string(by_id_.size()) +
      " subscriptions");
}

SubscriptionRegistry::SubscribeOutcome SubscriptionRegistry::Subscribe(
    std::string* subscription_id, const std::string& player_id, const std::string& game_id,
    const std::string& channel_id, int64_t subscribed_at_ms) {
  if (player_id.empty() || game_id.empty() || channel_id.empty()) {
    return SubscribeOutcome::kInvalid;
  }

  std::lock_guard<std::mutex> lock(mu_);
  // Validate a caller-supplied id before any mutation: an exact replay of
  // the record it names is idempotent, and reusing the key for a different
  // tuple would silently break duplicate detection.
  if (!subscription_id->empty()) {
    const auto existing_id = by_id_.find(*subscription_id);
    if (existing_id != by_id_.end()) {
      const auto& stored = existing_id->second;
      if (stored.player_id() == player_id && stored.game_id() == game_id &&
          stored.channel_id() == channel_id) {
        return SubscribeOutcome::kExisted;
      }
      return SubscribeOutcome::kInvalid;
    }
  }

  const auto key = TupleKey(player_id, game_id, channel_id);
  const auto existing_tuple = tuple_index_.find(key);
  if (existing_tuple != tuple_index_.end()) {
    const auto& stored_id = existing_tuple->second;
    if (subscription_id->empty()) {
      // Self-service re-subscribes (no id) keep the stored record — and
      // its id — stable instead of churning ids.
      *subscription_id = stored_id;
      return SubscribeOutcome::kExisted;
    }
    // The same tuple re-asserted under a new id replaces the old record
    // (the backend is the authority), like re-binding a game user.
    const auto stale = by_id_.find(stored_id);
    if (stale != by_id_.end()) {
      PersistDeleteLocked(stale->second.subscription_id());
      EraseEntryLocked(stale->second);
    }
  }

  if (subscription_id->empty()) {
    *subscription_id = MintSubscriptionId();
  }

  StoredChannelSubscription entry;
  entry.set_subscription_id(*subscription_id);
  entry.set_player_id(player_id);
  entry.set_game_id(game_id);
  entry.set_channel_id(channel_id);
  entry.set_subscribed_at_ms(subscribed_at_ms);

  tuple_index_[key] = *subscription_id;
  player_index_[player_id].insert(*subscription_id);
  channel_index_[{game_id, channel_id}].insert(*subscription_id);
  by_id_[*subscription_id] = std::move(entry);
  PersistLocked(by_id_[*subscription_id]);
  return SubscribeOutcome::kSubscribed;
}

bool SubscriptionRegistry::UnsubscribeById(const std::string& subscription_id) {
  if (subscription_id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = by_id_.find(subscription_id);
  if (it == by_id_.end()) {
    return false;
  }
  PersistDeleteLocked(it->second.subscription_id());
  EraseEntryLocked(it->second);
  return true;
}

bool SubscriptionRegistry::UnsubscribeByTuple(const std::string& player_id,
                                              const std::string& game_id,
                                              const std::string& channel_id) {
  if (player_id.empty() || game_id.empty() || channel_id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = tuple_index_.find(TupleKey(player_id, game_id, channel_id));
  if (it == tuple_index_.end()) {
    return false;
  }
  const auto entry = by_id_.find(it->second);
  if (entry == by_id_.end()) {
    return false;
  }
  PersistDeleteLocked(entry->second.subscription_id());
  EraseEntryLocked(entry->second);
  return true;
}

std::vector<StoredChannelSubscription> SubscriptionRegistry::GetForPlayer(
    const std::string& player_id, const std::string& game_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<StoredChannelSubscription> out;
  const auto it = player_index_.find(player_id);
  if (it == player_index_.end()) {
    return out;
  }
  out.reserve(it->second.size());
  for (const auto& subscription_id : it->second) {
    const auto entry = by_id_.find(subscription_id);
    if (entry != by_id_.end() && (game_id.empty() || entry->second.game_id() == game_id)) {
      out.push_back(entry->second);
    }
  }
  return out;
}

std::vector<StoredChannelSubscription> SubscriptionRegistry::GetForChannel(
    const std::string& game_id, const std::string& channel_id) const {
  std::vector<StoredChannelSubscription> out;
  if (game_id.empty() || channel_id.empty()) {
    return out;  // defensive: the handler validates before calling
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = channel_index_.find({game_id, channel_id});
  if (it == channel_index_.end()) {
    return out;
  }
  out.reserve(it->second.size());
  for (const auto& subscription_id : it->second) {
    const auto entry = by_id_.find(subscription_id);
    if (entry != by_id_.end()) {
      out.push_back(entry->second);
    }
  }
  return out;
}

size_t SubscriptionRegistry::Size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return by_id_.size();
}

void SubscriptionRegistry::EraseEntryLocked(const StoredChannelSubscription& entry) {
  // `entry` usually aliases a node inside by_id_, so the by_id_ erase must
  // come last: after it the reference is dangling.
  tuple_index_.erase(TupleKey(entry.player_id(), entry.game_id(), entry.channel_id()));
  const auto pit = player_index_.find(entry.player_id());
  if (pit != player_index_.end()) {
    pit->second.erase(entry.subscription_id());
    if (pit->second.empty()) {
      player_index_.erase(pit);
    }
  }
  const auto cit = channel_index_.find({entry.game_id(), entry.channel_id()});
  if (cit != channel_index_.end()) {
    cit->second.erase(entry.subscription_id());
    if (cit->second.empty()) {
      channel_index_.erase(cit);
    }
  }
  by_id_.erase(entry.subscription_id());
}

void SubscriptionRegistry::PersistLocked(const StoredChannelSubscription& entry) {
  if (!redis_) {
    return;
  }
  if (!redis_->Set(kEntryPrefix + entry.subscription_id(), entry.SerializeAsString())) {
    // Write-through is best-effort: memory stays authoritative and a later
    // subscribe/unsubscribe of the same record retries the write.
    chirp::common::Logger::Instance().Warn(
        "player channel subscription store: redis write failed for " +
        entry.subscription_id());
  }
}

void SubscriptionRegistry::PersistDeleteLocked(const std::string& subscription_id) {
  if (!redis_) {
    return;
  }
  if (!redis_->Del(kEntryPrefix + subscription_id)) {
    chirp::common::Logger::Instance().Warn(
        "player channel subscription store: redis delete failed for " + subscription_id);
  }
}

}  // namespace chirp::game_server_gateway
