#include "identity_registry.h"

#include "common/logger.h"

namespace chirp::server_gateway {

namespace {

constexpr const char* kEntryPrefix = "chirp:binding:entry:";

std::string GameUserKey(const std::string& game_id, const std::string& game_user_id) {
  return game_id + "|" + game_user_id;
}

}  // namespace

IdentityRegistry::IdentityRegistry(RedisFactory factory)
    : redis_factory_(std::move(factory)) {
  if (redis_factory_) {
    redis_ = redis_factory_();
  }
}

std::vector<StoredIdentityBinding> IdentityRegistry::LoadFromRedis() {
  if (!redis_) {
    return {};
  }
  const auto keys = redis_->Keys(kEntryPrefix + std::string("*"));
  std::vector<StoredIdentityBinding> loaded;
  loaded.reserve(keys.size());
  for (const auto& key : keys) {
    const auto value = redis_->Get(key);
    if (!value) {
      continue;
    }
    StoredIdentityBinding entry;
    if (!entry.ParseFromString(*value)) {
      chirp::common::Logger::Instance().Warn(
          "identity binding store: skipping unparseable entry " + key);
      continue;
    }
    loaded.push_back(std::move(entry));
  }
  return loaded;
}

void IdentityRegistry::Load() {
  const auto loaded = LoadFromRedis();
  if (loaded.empty() && !redis_) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& entry : loaded) {
    // Entries persisted under the same unique game user replace each other
    // exactly as a Bind would; load order then only matters for ties, which
    // the backend never creates (binding ids are unique).
    const auto key = std::make_pair(entry.game_id(), entry.game_user_id());
    const auto clash = game_user_index_.find(key);
    if (clash != game_user_index_.end()) {
      const auto stale = by_id_.find(clash->second);
      if (stale != by_id_.end()) {
        EraseEntryLocked(stale->second);
      }
    }
    game_user_index_[key] = entry.binding_id();
    player_index_[entry.player_id()].insert(entry.binding_id());
    by_id_[entry.binding_id()] = entry;
  }
  chirp::common::Logger::Instance().Info(
      "identity binding store: loaded " + std::to_string(by_id_.size()) + " bindings");
}

IdentityRegistry::BindOutcome IdentityRegistry::Bind(const std::string& binding_id,
                                                     const std::string& player_id,
                                                     const std::string& game_id,
                                                     const std::string& game_user_id,
                                                     int64_t bound_at_ms) {
  if (binding_id.empty() || player_id.empty() || game_id.empty() || game_user_id.empty()) {
    return BindOutcome::kInvalid;
  }

  std::lock_guard<std::mutex> lock(mu_);
  const auto existing = by_id_.find(binding_id);
  if (existing != by_id_.end()) {
    const auto& entry = existing->second;
    if (entry.player_id() == player_id && entry.game_id() == game_id &&
        entry.game_user_id() == game_user_id) {
      return BindOutcome::kExisted;
    }
    // Same idempotency key asserting a different tuple: the caller is
    // reusing keys, which would silently break duplicate detection.
    return BindOutcome::kInvalid;
  }

  StoredIdentityBinding entry;
  entry.set_binding_id(binding_id);
  entry.set_player_id(player_id);
  entry.set_game_id(game_id);
  entry.set_game_user_id(game_user_id);
  entry.set_bound_at_ms(bound_at_ms);

  // Re-asserting a bound game user replaces the old binding (account
  // switch, unlink + relink). The old record disappears from every index.
  const auto key = std::make_pair(game_id, game_user_id);
  const auto clash = game_user_index_.find(key);
  if (clash != game_user_index_.end()) {
    const auto stale = by_id_.find(clash->second);
    if (stale != by_id_.end()) {
      PersistDeleteLocked(stale->second.binding_id());
      EraseEntryLocked(stale->second);
    }
  }

  game_user_index_[key] = binding_id;
  player_index_[player_id].insert(binding_id);
  by_id_[binding_id] = std::move(entry);
  PersistLocked(by_id_[binding_id]);
  return BindOutcome::kBound;
}

bool IdentityRegistry::UnbindById(const std::string& binding_id) {
  if (binding_id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = by_id_.find(binding_id);
  if (it == by_id_.end()) {
    return false;
  }
  PersistDeleteLocked(it->second.binding_id());
  EraseEntryLocked(it->second);
  return true;
}

bool IdentityRegistry::UnbindByGameUser(const std::string& game_id,
                                        const std::string& game_user_id) {
  if (game_id.empty() || game_user_id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = game_user_index_.find(std::make_pair(game_id, game_user_id));
  if (it == game_user_index_.end()) {
    return false;
  }
  const auto entry = by_id_.find(it->second);
  if (entry == by_id_.end()) {
    return false;
  }
  PersistDeleteLocked(entry->second.binding_id());
  EraseEntryLocked(entry->second);
  return true;
}

std::vector<StoredIdentityBinding> IdentityRegistry::GetByPlayer(
    const std::string& player_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<StoredIdentityBinding> out;
  const auto it = player_index_.find(player_id);
  if (it == player_index_.end()) {
    return out;
  }
  out.reserve(it->second.size());
  for (const auto& binding_id : it->second) {
    const auto entry = by_id_.find(binding_id);
    if (entry != by_id_.end()) {
      out.push_back(entry->second);
    }
  }
  return out;
}

std::unique_ptr<std::string> IdentityRegistry::Resolve(const std::string& game_id,
                                                       const std::string& game_user_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = game_user_index_.find(std::make_pair(game_id, game_user_id));
  if (it == game_user_index_.end()) {
    return nullptr;
  }
  const auto entry = by_id_.find(it->second);
  if (entry == by_id_.end()) {
    return nullptr;
  }
  return std::make_unique<std::string>(entry->second.player_id());
}

size_t IdentityRegistry::Size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return by_id_.size();
}

void IdentityRegistry::EraseEntryLocked(const StoredIdentityBinding& entry) {
  // `entry` usually aliases a node inside by_id_, so the by_id_ erase must
  // come last: after it the reference is dangling.
  game_user_index_.erase(std::make_pair(entry.game_id(), entry.game_user_id()));
  const auto pit = player_index_.find(entry.player_id());
  if (pit != player_index_.end()) {
    pit->second.erase(entry.binding_id());
    if (pit->second.empty()) {
      player_index_.erase(pit);
    }
  }
  by_id_.erase(entry.binding_id());
}

void IdentityRegistry::PersistLocked(const StoredIdentityBinding& entry) {
  if (!redis_) {
    return;
  }
  if (!redis_->Set(kEntryPrefix + entry.binding_id(), entry.SerializeAsString())) {
    // Write-through is best-effort: memory stays authoritative and a later
    // bind/unbind of the same record retries the write.
    chirp::common::Logger::Instance().Warn(
        "identity binding store: redis write failed for " + entry.binding_id());
  }
}

void IdentityRegistry::PersistDeleteLocked(const std::string& binding_id) {
  if (!redis_) {
    return;
  }
  if (!redis_->Del(kEntryPrefix + binding_id)) {
    chirp::common::Logger::Instance().Warn(
        "identity binding store: redis delete failed for " + binding_id);
  }
}

}  // namespace chirp::server_gateway
