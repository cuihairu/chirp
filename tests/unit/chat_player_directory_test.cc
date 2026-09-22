// Coverage for the app-plane player directory (WP-8) after its move from
// game_server_gateway into app_chat: the three backing registries, the
// wire-facing handlers behind the dispatch, the hub fan-out tail, and the
// packet dispatcher itself. Redis is a fake in-memory map, so the
// write-through/restore paths run without a server.
#include "player_directory.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "proto/game_server_gateway.pb.h"

namespace {

namespace chat = chirp::chat;
namespace sg = chirp::game_server_gateway;

using chirp::common::AUTH_FAILED;
using chirp::common::INVALID_PARAM;
using chirp::common::OK;

// ---------------------------------------------------------------------------
// Registry plumbing shared with the write-through tests
// ---------------------------------------------------------------------------

// In-memory stand-in for the write-through client: the registry only needs
// Get/Set/Del/Keys. `fail` simulates a dead Redis (writes/reads no-op).
class FakeRedisClient : public chirp::network::RedisClient {
 public:
  explicit FakeRedisClient(std::shared_ptr<std::map<std::string, std::string>> store)
      : RedisClient("127.0.0.1", 1), store_(std::move(store)) {}

  std::optional<std::string> Get(const std::string& key) override {
    if (fail || fail_get) {
      return std::nullopt;
    }
    const auto it = store_->find(key);
    return it == store_->end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  bool Set(const std::string& key, const std::string& value) override {
    if (fail) {
      return false;
    }
    (*store_)[key] = value;
    return true;
  }
  bool Del(const std::string& key) override {
    if (fail) {
      return false;
    }
    // The real DEL answers 0 for a missing key and the client reports true:
    // the delete itself succeeded.
    store_->erase(key);
    return true;
  }
  std::vector<std::string> Keys(const std::string& pattern) override {
    std::vector<std::string> out;
    if (fail) {
      return out;
    }
    const std::string prefix = pattern.substr(0, pattern.size() - 1);
    for (const auto& [key, value] : *store_) {
      if (key.rfind(prefix, 0) == 0) {
        out.push_back(key);
      }
    }
    return out;
  }

  bool fail = false;
  // Keys() still works when only reads fail: Load must skip what it cannot
  // read instead of treating the whole store as empty.
  bool fail_get = false;

 private:
  std::shared_ptr<std::map<std::string, std::string>> store_;
};

// ---------------------------------------------------------------------------
// IdentityRegistry (WP-8 slice 1: player identity bindings)
// ---------------------------------------------------------------------------

sg::BindPlayerIdentityRequest MakeBindRequest(const std::string& binding_id,
                                              const std::string& player_id,
                                              const std::string& game_id,
                                              const std::string& game_user_id) {
  sg::BindPlayerIdentityRequest req;
  req.set_binding_id(binding_id);
  req.set_player_id(player_id);
  req.set_game_id(game_id);
  req.set_game_user_id(game_user_id);
  return req;
}

TEST(IdentityRegistryTest, BindLifecycleAndIdempotency) {
  chat::IdentityRegistry registry;
  EXPECT_EQ(registry.Size(), 0u);

  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  // The exact same tuple under the same idempotency key is a no-op.
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 2000),
            chat::IdentityRegistry::BindOutcome::kExisted);
  // The same key asserting a different tuple would silently break duplicate
  // detection and is rejected.
  EXPECT_EQ(registry.Bind("b1", "player-2", "game-a", "u-1", 3000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  // Empty fields are rejected (the handler validates first, the store
  // enforces the same contract).
  EXPECT_EQ(registry.Bind("", "player-1", "game-a", "u-2", 1000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "", "game-a", "u-2", 1000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "player-1", "", "u-2", 1000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-a", "", 1000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(IdentityRegistryTest, RebindingGameUserReplacesTheOldBinding) {
  chat::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  // The backend re-asserts: game user u-1 actually belongs to player-2 now.
  EXPECT_EQ(registry.Bind("b2", "player-2", "game-a", "u-1", 2000),
            chat::IdentityRegistry::BindOutcome::kBound);

  const auto player = registry.Resolve("game-a", "u-1");
  ASSERT_NE(player, nullptr);
  EXPECT_EQ(*player, "player-2");
  EXPECT_EQ(registry.GetByPlayer("player-1").size(), 0u);
  ASSERT_EQ(registry.GetByPlayer("player-2").size(), 1u);
  EXPECT_EQ(registry.GetByPlayer("player-2")[0].binding_id(), "b2");
  EXPECT_EQ(registry.GetByPlayer("player-2")[0].bound_at_ms(), 2000);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(IdentityRegistryTest, OnePlayerHoldsManyGameIdentities) {
  chat::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-b", "char-9", 2000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.GetByPlayer("player-1").size(), 2u);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
  EXPECT_EQ(*registry.Resolve("game-b", "char-9"), "player-1");
}

TEST(IdentityRegistryTest, UnbindByIdAndByPairAreIdempotent) {
  chat::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-b", "char-9", 2000),
            chat::IdentityRegistry::BindOutcome::kBound);

  EXPECT_TRUE(registry.UnbindById("b1"));
  EXPECT_FALSE(registry.UnbindById("b1"));  // already gone
  EXPECT_FALSE(registry.UnbindById("never-bound"));
  EXPECT_FALSE(registry.UnbindById(""));
  EXPECT_EQ(registry.Resolve("game-a", "u-1"), nullptr);

  EXPECT_TRUE(registry.UnbindByGameUser("game-b", "char-9"));
  EXPECT_FALSE(registry.UnbindByGameUser("game-b", "char-9"));
  EXPECT_FALSE(registry.UnbindByGameUser("game-b", ""));
  EXPECT_FALSE(registry.UnbindByGameUser("", "char-9"));
  EXPECT_EQ(registry.Size(), 0u);
}

TEST(IdentityRegistryTest, ResolveUnboundReturnsNull) {
  chat::IdentityRegistry registry;
  EXPECT_EQ(registry.Resolve("game-a", "u-1"), nullptr);
}

TEST(IdentityRegistryTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();

  chat::IdentityRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  // Loading an empty store is a clean no-op.
  writer.Load();

  EXPECT_EQ(writer.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(writer.Bind("b2", "player-2", "game-b", "char-9", 2000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(store->size(), 2u);

  // A fresh app_chat process restores every binding through Load.
  chat::IdentityRegistry reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 2u);
  EXPECT_EQ(*reader.Resolve("game-a", "u-1"), "player-1");
  ASSERT_EQ(reader.GetByPlayer("player-2").size(), 1u);
  EXPECT_EQ(reader.GetByPlayer("player-2")[0].game_id(), "game-b");

  // Unbind removes the persisted record.
  EXPECT_TRUE(writer.UnbindById("b1"));
  EXPECT_EQ(store->size(), 1u);

  // A corrupted record is skipped, not fatal.
  (*store)["chirp:binding:entry:junk"] = "\x01\x02not-a-proto";
  chat::IdentityRegistry tolerant([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  tolerant.Load();
  EXPECT_EQ(tolerant.Size(), 1u);
}

TEST(IdentityRegistryTest, LoadedPairClashAppliesReplaceRule) {
  // Two service instances persisted the same game user under different
  // binding ids (their write-throughs never saw each other). Load order
  // applies the same replace rule a Bind would: the later record wins.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::IdentityRegistry first([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  EXPECT_EQ(first.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  chat::IdentityRegistry second([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  EXPECT_EQ(second.Bind("b2", "player-2", "game-a", "u-1", 2000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(store->size(), 2u);

  chat::IdentityRegistry loader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  loader.Load();
  ASSERT_EQ(loader.Size(), 1u);
  EXPECT_EQ(*loader.Resolve("game-a", "u-1"), "player-2");
}

TEST(IdentityRegistryTest, LoadSkipsUnreadableRecords) {
  // Keys() lists a record but Get() cannot read it back (e.g. a flaky
  // replica): Load must skip the entry, not treat the store as empty.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::IdentityRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  EXPECT_EQ(writer.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(store->size(), 1u);

  chat::IdentityRegistry reader([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail_get = true;
    return client;
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 0u);
}

TEST(IdentityRegistryTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::IdentityRegistry registry([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  registry.Load();  // reads fail; must not crash or block

  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
  EXPECT_TRUE(registry.UnbindById("b1"));
  EXPECT_EQ(store->size(), 0u);  // nothing ever reached Redis
}

TEST(IdentityRegistryTest, MemoryOnlyByDefault) {
  chat::IdentityRegistry registry;
  registry.Load();  // no factory: nothing to load
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
}

// ---------------------------------------------------------------------------
// SubscriptionRegistry (WP-8 slice 2: player channel subscriptions)
// ---------------------------------------------------------------------------

sg::SubscribePlayerChannelRequest MakeSubscribeRequest(const std::string& subscription_id,
                                                       const std::string& player_id,
                                                       const std::string& game_id,
                                                       const std::string& channel_id) {
  sg::SubscribePlayerChannelRequest req;
  req.set_subscription_id(subscription_id);
  req.set_player_id(player_id);
  req.set_game_id(game_id);
  req.set_channel_id(channel_id);
  return req;
}

TEST(SubscriptionRegistryTest, SubscribeLifecycleAndIdempotency) {
  chat::SubscriptionRegistry registry;
  EXPECT_EQ(registry.Size(), 0u);

  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  // The exact same tuple under the same idempotency key is a no-op.
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kExisted);
  // The same key asserting a different tuple would silently break duplicate
  // detection and is rejected.
  std::string reused = "s1";
  EXPECT_EQ(registry.Subscribe(&reused, "player-2", "game-a", "world-1", 3000),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  // Empty tuple fields are rejected (the handler validates first, the store
  // enforces the same contract).
  std::string keep = "s2";
  EXPECT_EQ(registry.Subscribe(&keep, "", "game-a", "world-2", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Subscribe(&keep, "player-1", "", "world-2", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Subscribe(&keep, "player-1", "game-a", "", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, ReSubscribingTupleUnderNewIdReplaces) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  // The same tuple re-asserted under a new id replaces the old record.
  std::string reid = "s2";
  EXPECT_EQ(registry.Subscribe(&reid, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  ASSERT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
  EXPECT_EQ(registry.GetForPlayer("player-1", "")[0].subscription_id(), "s2");
  EXPECT_EQ(registry.GetForPlayer("player-1", "")[0].subscribed_at_ms(), 2000);
  EXPECT_FALSE(registry.UnsubscribeById("s1"));  // replaced away
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, SelfSubscribeMintsAStableId) {
  chat::SubscriptionRegistry registry;
  std::string minted;
  EXPECT_EQ(registry.Subscribe(&minted, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_TRUE(minted.rfind("sub-", 0) == 0);

  // An id-less re-subscribe (the self-service path) keeps the record — and
  // its id — stable instead of churning ids.
  std::string again;
  EXPECT_EQ(registry.Subscribe(&again, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kExisted);
  EXPECT_EQ(again, minted);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, UnsubscribeByIdAndByTupleAreIdempotent) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  // Half a triple is not a selector; only the full triple (or the id) is.
  EXPECT_FALSE(registry.UnsubscribeByTuple("player-1", "game-a", ""));
  EXPECT_FALSE(registry.UnsubscribeByTuple("", "game-a", "world-1"));

  EXPECT_TRUE(registry.UnsubscribeByTuple("player-1", "game-a", "world-1"));
  EXPECT_FALSE(registry.UnsubscribeByTuple("player-1", "game-a", "world-1"));
  EXPECT_FALSE(registry.UnsubscribeById("s1"));

  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_TRUE(registry.UnsubscribeById("s1"));
  EXPECT_FALSE(registry.UnsubscribeById("s1"));
  EXPECT_FALSE(registry.UnsubscribeById(""));  // empty id is never a selector
  EXPECT_EQ(registry.Size(), 0u);
}

TEST(SubscriptionRegistryTest, GetForPlayerWithGameFilter) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-b", "ranked-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s3";
  ASSERT_EQ(registry.Subscribe(&id, "player-2", "game-a", "world-1", 3000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 2u);
  const auto filtered = registry.GetForPlayer("player-1", "game-a");
  ASSERT_EQ(filtered.size(), 1u);
  EXPECT_EQ(filtered[0].subscription_id(), "s1");
  EXPECT_EQ(registry.GetForPlayer("player-1", "game-c").size(), 0u);
  EXPECT_EQ(registry.GetForPlayer("nobody", "").size(), 0u);
}

// --- reverse (game, channel) index: the fan-in source list (slice 3) ---

namespace {

std::set<std::string> SubscriberIds(const std::vector<sg::StoredChannelSubscription>& subs) {
  std::set<std::string> out;
  for (const auto& sub : subs) {
    out.insert(sub.player_id());
  }
  return out;
}

}  // namespace

TEST(SubscriptionRegistryTest, ChannelIndexReturnsSubscribersAcrossPlayers) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  ASSERT_EQ(registry.Subscribe(&id, "player-2", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s3";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-2", 3000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s4";
  ASSERT_EQ(registry.Subscribe(&id, "player-3", "game-b", "world-1", 4000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  const auto subscribers = registry.GetForChannel("game-a", "world-1");
  ASSERT_EQ(subscribers.size(), 2u);
  EXPECT_EQ(SubscriberIds(subscribers), (std::set<std::string>{"player-1", "player-2"}));

  EXPECT_EQ(registry.GetForChannel("game-a", "world-2").size(), 1u);
  EXPECT_EQ(registry.GetForChannel("game-b", "world-1").size(), 1u);
}

TEST(SubscriptionRegistryTest, ChannelIndexUnknownTupleIsEmpty) {
  chat::SubscriptionRegistry registry;
  EXPECT_TRUE(registry.GetForChannel("game-a", "world-1").empty());
  EXPECT_TRUE(registry.GetForChannel("", "world-1").empty());
  EXPECT_TRUE(registry.GetForChannel("game-a", "").empty());
}

TEST(SubscriptionRegistryTest, ChannelIndexClearedByBothUnsubscribePaths) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  EXPECT_TRUE(registry.UnsubscribeByTuple("player-1", "game-a", "world-1"));
  EXPECT_TRUE(registry.GetForChannel("game-a", "world-1").empty());

  id = "s2";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(registry.GetForChannel("game-a", "world-1").size(), 1u);

  EXPECT_TRUE(registry.UnsubscribeById("s2"));
  EXPECT_TRUE(registry.GetForChannel("game-a", "world-1").empty());
}

TEST(SubscriptionRegistryTest, ChannelIndexSurvivesTupleReplace) {
  chat::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  // The same tuple re-asserted under a new id: the replaced-away record must
  // leave the channel index together with the by-id map.
  id = "s2";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  const auto subscribers = registry.GetForChannel("game-a", "world-1");
  ASSERT_EQ(subscribers.size(), 1u);
  EXPECT_EQ(subscribers[0].subscription_id(), "s2");
  EXPECT_FALSE(registry.UnsubscribeById("s1"));  // replaced away
}

TEST(SubscriptionRegistryTest, ChannelIndexRestoredByLoad) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::SubscriptionRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  std::string id = "s1";
  ASSERT_EQ(writer.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  ASSERT_EQ(writer.Subscribe(&id, "player-2", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  chat::SubscriptionRegistry reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  const auto subscribers = reader.GetForChannel("game-a", "world-1");
  ASSERT_EQ(subscribers.size(), 2u);
  EXPECT_EQ(SubscriberIds(subscribers), (std::set<std::string>{"player-1", "player-2"}));
}

TEST(SubscriptionRegistryTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();

  chat::SubscriptionRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  writer.Load();  // loading an empty store is a clean no-op

  std::string id = "s1";
  EXPECT_EQ(writer.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  EXPECT_EQ(writer.Subscribe(&id, "player-2", "game-b", "ranked-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(store->size(), 2u);

  // A fresh app_chat process restores every subscription through Load.
  chat::SubscriptionRegistry reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 2u);
  ASSERT_EQ(reader.GetForPlayer("player-2", "").size(), 1u);
  EXPECT_EQ(reader.GetForPlayer("player-2", "")[0].game_id(), "game-b");

  // Unsubscribe removes the persisted record.
  EXPECT_TRUE(writer.UnsubscribeById("s1"));
  EXPECT_EQ(store->size(), 1u);

  // A corrupted record is skipped, not fatal.
  (*store)["chirp:subscription:entry:junk"] = "\x01\x02not-a-proto";
  chat::SubscriptionRegistry tolerant([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  tolerant.Load();
  EXPECT_EQ(tolerant.Size(), 1u);
}

TEST(SubscriptionRegistryTest, LoadedTupleClashAppliesReplaceRule) {
  // Two service instances persisted the same tuple under different ids
  // (their write-throughs never saw each other). Load order applies the same
  // replace rule a Subscribe would: the later record wins.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::SubscriptionRegistry first([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  std::string id = "s1";
  ASSERT_EQ(first.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  chat::SubscriptionRegistry second([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  id = "s2";
  ASSERT_EQ(second.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  ASSERT_EQ(store->size(), 2u);

  chat::SubscriptionRegistry loader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  loader.Load();
  ASSERT_EQ(loader.Size(), 1u);
  EXPECT_EQ(loader.GetForPlayer("player-1", "")[0].subscription_id(), "s2");
}

TEST(SubscriptionRegistryTest, LoadSkipsUnreadableRecords) {
  // Keys() lists a record but Get() cannot read it back (e.g. a flaky
  // replica): Load must skip the entry, not treat the store as empty.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::SubscriptionRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  std::string id = "s1";
  ASSERT_EQ(writer.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  ASSERT_EQ(store->size(), 1u);

  chat::SubscriptionRegistry reader([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail_get = true;
    return client;
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 0u);
}

TEST(SubscriptionRegistryTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::SubscriptionRegistry registry([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  registry.Load();  // reads fail; must not crash or block

  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
  EXPECT_TRUE(registry.UnsubscribeById("s1"));
  EXPECT_EQ(store->size(), 0u);  // nothing ever reached Redis
}

TEST(SubscriptionRegistryTest, MemoryOnlyByDefault) {
  chat::SubscriptionRegistry registry;
  registry.Load();  // no factory: nothing to load
  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
}

// ---------------------------------------------------------------------------
// UnreadLedger (WP-8 slice 4: unified unread badge ledger)
// ---------------------------------------------------------------------------

TEST(UnreadLedgerTest, IncrementAccumulatesPerChannel) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-a", "world-2");
  ledger.Increment("player-1", "game-b", "dungeon-1");
  ledger.Increment("player-2", "game-a", "world-1");

  EXPECT_EQ(ledger.Size(), 4u);
  const auto summary = ledger.GetSummary("player-1", "");
  ASSERT_EQ(summary.size(), 3u);
  // The inner map keeps (game, channel) sorted: summaries are stable.
  EXPECT_EQ(summary[0].game_id(), "game-a");
  EXPECT_EQ(summary[0].channel_id(), "world-1");
  EXPECT_EQ(summary[0].unread_count(), 2);
  EXPECT_EQ(summary[1].game_id(), "game-a");
  EXPECT_EQ(summary[1].channel_id(), "world-2");
  EXPECT_EQ(summary[1].unread_count(), 1);
  EXPECT_EQ(summary[2].game_id(), "game-b");
  EXPECT_EQ(summary[2].unread_count(), 1);

  const auto other = ledger.GetSummary("player-2", "");
  ASSERT_EQ(other.size(), 1u);
  EXPECT_EQ(other[0].unread_count(), 1);
}

TEST(UnreadLedgerTest, MarkSingleChannelIsIdempotent) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-a", "world-2");

  // Unknown targets are idempotent no-ops.
  EXPECT_EQ(ledger.MarkRead("player-1", "game-a", "unfollowed"), 0u);
  EXPECT_EQ(ledger.MarkRead("player-1", "game-x", "world-1"), 0u);
  EXPECT_EQ(ledger.MarkRead("player-unknown", "game-a", "world-1"), 0u);

  EXPECT_EQ(ledger.MarkRead("player-1", "game-a", "world-1"), 1u);
  // Already cleared: clearing again finds nothing.
  EXPECT_EQ(ledger.MarkRead("player-1", "game-a", "world-1"), 0u);

  const auto summary = ledger.GetSummary("player-1", "");
  ASSERT_EQ(summary.size(), 1u);
  EXPECT_EQ(summary[0].channel_id(), "world-2");  // the neighbor survives
}

TEST(UnreadLedgerTest, MarkWholeGameClearsOnlyThatGame) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-a", "world-2");
  ledger.Increment("player-1", "game-b", "dungeon-1");
  ledger.Increment("player-2", "game-a", "world-1");

  EXPECT_EQ(ledger.MarkRead("player-1", "game-a", ""), 2u);
  const auto summary = ledger.GetSummary("player-1", "");
  ASSERT_EQ(summary.size(), 1u);
  EXPECT_EQ(summary[0].game_id(), "game-b");
  EXPECT_EQ(ledger.GetSummary("player-2", "").size(), 1u);  // other players untouched
}

TEST(UnreadLedgerTest, MarkAllClearsEverything) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-b", "dungeon-1");
  ledger.Increment("player-2", "game-a", "world-1");

  EXPECT_EQ(ledger.MarkRead("player-1", "", ""), 2u);
  EXPECT_TRUE(ledger.GetSummary("player-1", "").empty());
  EXPECT_EQ(ledger.Size(), 1u);  // player-2 keeps their entry
  EXPECT_EQ(ledger.MarkRead("player-1", "", ""), 0u);  // idempotent
}

TEST(UnreadLedgerTest, SummaryWithGameFilter) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");
  ledger.Increment("player-1", "game-a", "world-2");
  ledger.Increment("player-1", "game-b", "dungeon-1");

  const auto filtered = ledger.GetSummary("player-1", "game-a");
  ASSERT_EQ(filtered.size(), 2u);
  int32_t total = 0;
  for (const auto& entry : filtered) {
    total += entry.unread_count();
  }
  EXPECT_EQ(total, 2);
}

TEST(UnreadLedgerTest, MalformedSelectorClearsNothing) {
  chat::UnreadLedger ledger;
  ledger.Increment("player-1", "game-a", "world-1");

  // channel_id without game_id clears nothing (the handler rejects the
  // request outright; the ledger answers 0 defensively).
  EXPECT_EQ(ledger.MarkRead("player-1", "", "world-1"), 0u);
  EXPECT_EQ(ledger.Size(), 1u);
  // Empty player: defensive no-op.
  EXPECT_EQ(ledger.MarkRead("", "game-a", "world-1"), 0u);
  // Empty increment components: defensive no-ops (the fan-in path validates
  // before calling, but the ledger must not store garbage anyway).
  ledger.Increment("", "game-a", "world-1");
  ledger.Increment("player-1", "", "world-1");
  ledger.Increment("player-1", "game-a", "");
  EXPECT_EQ(ledger.Size(), 1u);
}

TEST(UnreadLedgerTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::UnreadLedger writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  writer.Load();  // loading an empty store is a clean no-op

  writer.Increment("player-1", "game-a", "world-1");
  writer.Increment("player-1", "game-a", "world-1");
  writer.Increment("player-1", "game-a", "world-2");
  EXPECT_EQ(store->size(), 2u);

  // A fresh app_chat process restores every counter through Load.
  chat::UnreadLedger reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 2u);
  const auto summary = reader.GetSummary("player-1", "game-a");
  ASSERT_EQ(summary.size(), 2u);
  EXPECT_EQ(summary[0].channel_id(), "world-1");
  EXPECT_EQ(summary[0].unread_count(), 2);

  // Marking read deletes the persisted record, so a reload must not
  // resurrect the counter.
  EXPECT_EQ(writer.MarkRead("player-1", "game-a", "world-1"), 1u);
  EXPECT_EQ(store->size(), 1u);
  chat::UnreadLedger reloaded([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reloaded.Load();
  const auto after = reloaded.GetSummary("player-1", "");
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after[0].channel_id(), "world-2");
}

TEST(UnreadLedgerTest, LoadSkipsUnreadableRecords) {
  // Keys() lists a record but Get() cannot read it back (e.g. a flaky
  // replica): Load must skip the entry, not treat the store as empty.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::UnreadLedger writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  writer.Increment("player-1", "game-a", "world-1");
  ASSERT_EQ(store->size(), 1u);

  chat::UnreadLedger reader([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail_get = true;
    return client;
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 0u);
}

TEST(UnreadLedgerTest, LoadSkipsCorruptAndZeroRecords) {
  // A corrupted record and a zero-count record (cleared entries are
  // deleted, never written as zero — anything hand-written is legacy
  // state) are both skipped instead of confusing the summary.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  (*store)["chirp:unread:entry:junk"] = "\x01\x02not-a-proto";
  sg::StoredUnreadEntry zero;
  zero.set_player_id("player-1");
  zero.set_game_id("game-a");
  zero.set_channel_id("world-1");
  zero.set_unread_count(0);
  (*store)["chirp:unread:entry:player-1:game-a:world-1"] = zero.SerializeAsString();

  chat::UnreadLedger ledger([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  ledger.Load();
  EXPECT_EQ(ledger.Size(), 0u);
}

TEST(UnreadLedgerTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::UnreadLedger ledger([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  ledger.Load();  // reads fail; must not crash or block

  ledger.Increment("player-1", "game-a", "world-1");
  EXPECT_EQ(ledger.GetSummary("player-1", "").size(), 1u);
  EXPECT_EQ(ledger.MarkRead("player-1", "game-a", "world-1"), 1u);
  EXPECT_EQ(store->size(), 0u);  // nothing ever reached Redis
}

TEST(UnreadLedgerTest, MemoryOnlyByDefault) {
  chat::UnreadLedger ledger;
  ledger.Load();  // no factory: nothing to load
  ledger.Increment("player-1", "game-a", "world-1");
  EXPECT_EQ(ledger.GetSummary("player-1", "").size(), 1u);
}

// ---------------------------------------------------------------------------
// PlayerDirectory handlers (wire-facing validation)
// ---------------------------------------------------------------------------

// A fresh directory per test; unread entries are seeded through the real
// subscribe + fan-out path so the badge integration stays exercised.
class PlayerDirectoryHandlerTest : public ::testing::Test {
 protected:
  chat::PlayerDirectory directory_{chat::PlayerDirectory::Options()};

  // Subscribes (self-service, minted id) and pushes one uplink through the
  // channel, which is exactly one unread increment for the player.
  void SeedUnread(const std::string& player, const std::string& game,
                  const std::string& channel) {
    const auto sub =
        directory_.HandleSubscribePlayerChannel(MakeSubscribeRequest("", player, game, channel));
    ASSERT_EQ(sub.code(), OK);
    chirp::gateway::ChannelMessageNotify notify;
    notify.set_game_id(game);
    notify.set_channel_id(channel);
    ASSERT_GT(directory_.FanoutChannelMessage(notify), 0u);
  }
};

TEST_F(PlayerDirectoryHandlerTest, BindHandlerValidatesAndReportsExisted) {
  auto resp = directory_.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_FALSE(resp.existed());
  EXPECT_EQ(resp.binding_id(), "b1");

  resp = directory_.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.existed());

  resp = directory_.HandleBindPlayerIdentity(MakeBindRequest("b2", "", "game-a", "u-2"));
  EXPECT_EQ(resp.code(), INVALID_PARAM);
}

TEST_F(PlayerDirectoryHandlerTest, UnbindHandlerRequiresExactlyOneSelector) {
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_binding_id("b1");
              req.set_game_id("game-a");  // both selectors: ambiguous
              req.set_game_user_id("u-1");
              return req;
            }()).code(),
            INVALID_PARAM);

  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;  // no selector at all
              return req;
            }()).code(),
            INVALID_PARAM);

  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_id("game-a");  // half a pair
              return req;
            }()).code(),
            INVALID_PARAM);

  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_user_id("u-1");  // the other half
              return req;
            }()).code(),
            INVALID_PARAM);

  // A complete pair unbinds; unknown targets still answer OK (idempotent).
  EXPECT_EQ(directory_.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_id("game-a");
              req.set_game_user_id("u-1");
              return req;
            }()).code(),
            OK);

  sg::GetPlayerIdentitiesRequest gone;
  gone.set_player_id("player-1");
  EXPECT_EQ(directory_.HandleGetPlayerIdentities(gone).bindings_size(), 0);
}

TEST_F(PlayerDirectoryHandlerTest, GetHandlerReturnsBindingsForPlayer) {
  EXPECT_EQ(directory_.HandleGetPlayerIdentities([] {
              sg::GetPlayerIdentitiesRequest req;  // empty player_id
              return req;
            }()).code(),
            INVALID_PARAM);

  EXPECT_EQ(directory_.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  EXPECT_EQ(directory_.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "char-9")).code(), OK);

  sg::GetPlayerIdentitiesRequest req;
  req.set_player_id("player-1");
  const auto resp = directory_.HandleGetPlayerIdentities(req);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.bindings_size(), 2);

  // Unknown players answer OK with an empty list.
  sg::GetPlayerIdentitiesRequest unknown;
  unknown.set_player_id("nobody");
  const auto empty = directory_.HandleGetPlayerIdentities(unknown);
  EXPECT_EQ(empty.code(), OK);
  EXPECT_EQ(empty.bindings_size(), 0);
}

TEST_F(PlayerDirectoryHandlerTest, ResolveHandlerValidatesAndReportsUnbound) {
  EXPECT_EQ(directory_.HandleResolveGameUser([] {
              sg::ResolveGameUserRequest req;
              req.set_game_id("game-a");  // game_user_id missing
              return req;
            }()).code(),
            INVALID_PARAM);

  sg::ResolveGameUserRequest unbound;
  unbound.set_game_id("game-a");
  unbound.set_game_user_id("u-1");
  auto resp = directory_.HandleResolveGameUser(unbound);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.player_id(), "");

  EXPECT_EQ(directory_.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  resp = directory_.HandleResolveGameUser(unbound);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.player_id(), "player-1");
}

TEST_F(PlayerDirectoryHandlerTest, SubscribeHandlerValidatesMintsAndReportsExisted) {
  // Missing tuple fields are a bad request.
  sg::SubscribePlayerChannelRequest missing;
  missing.set_player_id("player-1");
  missing.set_game_id("game-a");
  EXPECT_EQ(directory_.HandleSubscribePlayerChannel(missing).code(), INVALID_PARAM);

  // A backend-asserted id is echoed; the same request replays as existed.
  auto resp = directory_.HandleSubscribePlayerChannel(
      MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.subscription_id(), "s1");
  EXPECT_FALSE(resp.existed());
  resp = directory_.HandleSubscribePlayerChannel(
      MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.existed());

  // The same id asserting a different tuple is a key-reuse: rejected.
  resp = directory_.HandleSubscribePlayerChannel(
      MakeSubscribeRequest("s1", "player-2", "game-a", "world-1"));
  EXPECT_EQ(resp.code(), INVALID_PARAM);

  // An empty id (the app edge's self-service path) gets a minted one.
  resp = directory_.HandleSubscribePlayerChannel(
      MakeSubscribeRequest("", "player-2", "game-b", "ranked-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_FALSE(resp.existed());
  EXPECT_TRUE(resp.subscription_id().rfind("sub-", 0) == 0);
}

TEST_F(PlayerDirectoryHandlerTest, UnsubscribeHandlerRequiresExactlyOneSelector) {
  sg::UnsubscribePlayerChannelRequest req;
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);

  req.set_subscription_id("s1");
  req.set_player_id("player-1");
  req.set_game_id("game-a");
  req.set_channel_id("world-1");
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);  // both

  req.clear_subscription_id();
  req.clear_channel_id();  // half a triple
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);

  req.set_channel_id("world-1");  // full triple, unknown target: idempotent OK
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(req).code(), OK);

  // By id, on an existing subscription.
  ASSERT_EQ(directory_.HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s9", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  req.set_subscription_id("s9");
  req.clear_player_id();
  req.clear_game_id();
  req.clear_channel_id();
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(req).code(), OK);
}

TEST_F(PlayerDirectoryHandlerTest, GetHandlerReturnsSubscriptionsForPlayer) {
  sg::GetPlayerSubscriptionsRequest empty;
  EXPECT_EQ(directory_.HandleGetPlayerSubscriptions(empty).code(), INVALID_PARAM);

  ASSERT_EQ(directory_.HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  ASSERT_EQ(directory_.HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s2", "player-1", "game-b", "ranked-1"))
                .code(),
            OK);

  sg::GetPlayerSubscriptionsRequest all;
  all.set_player_id("player-1");
  auto resp = directory_.HandleGetPlayerSubscriptions(all);
  EXPECT_EQ(resp.code(), OK);
  ASSERT_EQ(resp.subscriptions_size(), 2);

  all.set_game_id("game-b");
  resp = directory_.HandleGetPlayerSubscriptions(all);
  ASSERT_EQ(resp.subscriptions_size(), 1);
  EXPECT_EQ(resp.subscriptions(0).subscription_id(), "s2");
}

TEST_F(PlayerDirectoryHandlerTest, MarkHandlerValidatesPlayerAndSelector) {
  sg::MarkChannelsReadRequest req;
  EXPECT_EQ(directory_.HandleMarkChannelsRead(req).code(), INVALID_PARAM);  // no player

  req.set_player_id("player-1");
  req.set_channel_id("world-1");
  EXPECT_EQ(directory_.HandleMarkChannelsRead(req).code(), INVALID_PARAM);  // channel w/o game

  SeedUnread("player-1", "game-a", "world-1");
  SeedUnread("player-1", "game-a", "world-2");

  req.set_game_id("game-a");
  req.set_channel_id("world-1");
  auto resp = directory_.HandleMarkChannelsRead(req);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.cleared(), 1);

  req.set_channel_id("");  // whole game
  resp = directory_.HandleMarkChannelsRead(req);
  EXPECT_EQ(resp.cleared(), 1);  // world-2 was left

  resp = directory_.HandleMarkChannelsRead(req);  // repeating the game clear
  EXPECT_EQ(resp.cleared(), 0);                   // is an idempotent no-op
}

TEST_F(PlayerDirectoryHandlerTest, SummaryHandlerValidatesPlayerAndFilters) {
  sg::GetUnreadSummaryRequest req;
  EXPECT_EQ(directory_.HandleGetUnreadSummary(req).code(), INVALID_PARAM);  // no player

  req.set_player_id("player-1");
  auto resp = directory_.HandleGetUnreadSummary(req);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.entries_size(), 0);
  EXPECT_EQ(resp.total_unread(), 0);

  SeedUnread("player-1", "game-a", "world-1");
  SeedUnread("player-1", "game-a", "world-1");  // a second uplink: count 2
  SeedUnread("player-1", "game-a", "world-2");
  SeedUnread("player-1", "game-b", "dungeon-1");

  resp = directory_.HandleGetUnreadSummary(req);
  ASSERT_EQ(resp.entries_size(), 3);
  EXPECT_EQ(resp.total_unread(), 4);

  req.set_game_id("game-b");
  resp = directory_.HandleGetUnreadSummary(req);
  ASSERT_EQ(resp.entries_size(), 1);
  EXPECT_EQ(resp.entries(0).game_id(), "game-b");
  EXPECT_EQ(resp.total_unread(), 1);
}

// ---------------------------------------------------------------------------
// PlayerDirectory::FanoutChannelMessage (WP-8 slice 3, hub side)
// ---------------------------------------------------------------------------

class FanoutTest : public ::testing::Test {
 protected:
  struct Copy {
    std::string player_id;
    chirp::gateway::ChannelMessageNotify notify;
  };

  chat::PlayerDirectory::Options BaseOptions() {
    chat::PlayerDirectory::Options options;
    options.deliver_copy =
        [this](const std::string& player_id, const chirp::gateway::ChannelMessageNotify& notify) {
          copies.push_back({player_id, notify});
        };
    return options;
  }

  // Self-service subscribe through the directory's own handler.
  void SubscribeVia(chat::PlayerDirectory& directory, const std::string& player,
                    const std::string& game, const std::string& channel) {
    ASSERT_EQ(directory.HandleSubscribePlayerChannel(MakeSubscribeRequest("", player, game, channel))
                  .code(),
              OK);
  }

  chirp::gateway::ChannelMessageNotify Uplink(const std::string& game,
                                              const std::string& channel) {
    chirp::gateway::ChannelMessageNotify notify;
    notify.set_game_id(game);
    notify.set_channel_id(channel);
    notify.mutable_message()->set_content("hello travelers");
    return notify;
  }

  std::vector<Copy> copies;
};

TEST_F(FanoutTest, DeliversOnePrivateCopyPerSubscriber) {
  chat::PlayerDirectory directory(BaseOptions());
  SubscribeVia(directory, "player-1", "game-a", "world-1");
  SubscribeVia(directory, "player-2", "game-a", "world-1");

  EXPECT_EQ(directory.FanoutChannelMessage(Uplink("game-a", "world-1")), 2u);
  ASSERT_EQ(copies.size(), 2u);
  std::set<std::string> receivers;
  for (const auto& copy : copies) {
    EXPECT_EQ(copy.notify.message().content(), "hello travelers");
    EXPECT_EQ(copy.notify.game_id(), "game-a");
    receivers.insert(copy.player_id);
  }
  EXPECT_EQ(receivers, (std::set<std::string>{"player-1", "player-2"}));

  // Each accepted copy is one unhandled notification for its recipient.
  for (const char* player : {"player-1", "player-2"}) {
    sg::GetUnreadSummaryRequest req;
    req.set_player_id(player);
    const auto resp = directory.HandleGetUnreadSummary(req);
    ASSERT_EQ(resp.entries_size(), 1);
    EXPECT_EQ(resp.entries(0).game_id(), "game-a");
    EXPECT_EQ(resp.entries(0).channel_id(), "world-1");
    EXPECT_EQ(resp.entries(0).unread_count(), 1);
  }
}

TEST_F(FanoutTest, UnknownChannelIsASilentNoOp) {
  chat::PlayerDirectory directory(BaseOptions());
  EXPECT_EQ(directory.FanoutChannelMessage(Uplink("game-a", "unfollowed")), 0u);
  EXPECT_TRUE(copies.empty());

  sg::GetUnreadSummaryRequest req;
  req.set_player_id("player-1");
  EXPECT_EQ(directory.HandleGetUnreadSummary(req).total_unread(), 0);
}

TEST_F(FanoutTest, OverCapIsDropped) {
  chat::PlayerDirectory::Options options = BaseOptions();
  options.max_fanout_per_message = 1;
  chat::PlayerDirectory directory(std::move(options));
  SubscribeVia(directory, "player-1", "game-a", "world-1");
  SubscribeVia(directory, "player-2", "game-a", "world-1");

  // Same contract the old FanoutInject enforced on injects: a channel with
  // more subscribers than the bound is dropped whole, not truncated.
  EXPECT_EQ(directory.FanoutChannelMessage(Uplink("game-a", "world-1")), 0u);
  EXPECT_TRUE(copies.empty());
  for (const char* player : {"player-1", "player-2"}) {
    sg::GetUnreadSummaryRequest req;
    req.set_player_id(player);
    EXPECT_EQ(directory.HandleGetUnreadSummary(req).total_unread(), 0);
  }
}

TEST_F(FanoutTest, NullDelivererStillCountsUnread) {
  // The hook is optional (chat owns the actual delivery): the handoff count
  // and the badge increment happen regardless.
  chat::PlayerDirectory::Options options;
  options.deliver_copy = nullptr;
  chat::PlayerDirectory directory(std::move(options));
  SubscribeVia(directory, "player-1", "game-a", "world-1");

  EXPECT_EQ(directory.FanoutChannelMessage(Uplink("game-a", "world-1")), 1u);
  sg::GetUnreadSummaryRequest req;
  req.set_player_id("player-1");
  const auto resp = directory.HandleGetUnreadSummary(req);
  ASSERT_EQ(resp.entries_size(), 1);
  EXPECT_EQ(resp.entries(0).unread_count(), 1);
}

// ---------------------------------------------------------------------------
// DispatchPlayerDirectoryPacket (trust gate + block routing)
// ---------------------------------------------------------------------------

// Records the framed bytes the dispatch writes so tests can decode the
// response packets exactly like a peer would.
class FakeSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override {
    // runtime::SendPacket emits u32_be length prefix + body; Decode expects
    // the bare body, so strip (and sanity-check) the prefix first.
    if (bytes.size() < 4) {
      ++undecodable;
      return;
    }
    const uint32_t len = (static_cast<uint32_t>(bytes[0]) << 24) |
                         (static_cast<uint32_t>(bytes[1]) << 16) |
                         (static_cast<uint32_t>(bytes[2]) << 8) |
                         static_cast<uint32_t>(bytes[3]);
    if (len + 4 != bytes.size()) {
      ++undecodable;
      return;
    }
    chirp::gateway::Packet pkt;
    if (chirp::network::ProtobufFraming::Decode(bytes.substr(4), &pkt)) {
      sent.push_back(std::move(pkt));
    } else {
      ++undecodable;
    }
  }
  void SendAndClose(std::string bytes) override {
    Send(std::move(bytes));
    closed = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1:0"; }

  template <typename T>
  std::vector<T> Decode(chirp::gateway::MsgID msg_id) const {
    std::vector<T> out;
    for (const auto& pkt : sent) {
      if (pkt.msg_id() != msg_id) {
        continue;
      }
      T message;
      message.ParseFromString(pkt.body());
      out.push_back(std::move(message));
    }
    return out;
  }

  std::vector<chirp::gateway::Packet> sent;
  int undecodable = 0;
  bool closed = false;
};

chirp::gateway::Packet MakePacket(chirp::gateway::MsgID id, const google::protobuf::Message& body,
                                  int64_t seq = 42) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  return pkt;
}

class DispatchTest : public ::testing::Test {
 protected:
  std::shared_ptr<FakeSession> session = std::make_shared<FakeSession>();
  chat::PlayerDirectory directory_{chat::PlayerDirectory::Options()};
  std::unordered_set<const chirp::network::Session*> trusted;
};

TEST_F(DispatchTest, IgnoresPacketsOutsideTheBlock) {
  sg::EventPublishRequest unrelated;  // any body works; only the id matters
  EXPECT_FALSE(chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(chirp::gateway::SEND_MESSAGE_REQ, unrelated), session, directory_, &trusted));
  EXPECT_TRUE(session->sent.empty());
}

TEST_F(DispatchTest, DeniesUntrustedDials) {
  // A null trust set and an empty one are both untrusted.
  const auto req = MakePacket(chirp::gateway::BIND_PLAYER_IDENTITY_REQ,
                              MakeBindRequest("b1", "player-1", "game-a", "u-1"));
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(req, session, directory_, nullptr));
  ASSERT_EQ(session->sent.size(), 1u);

  auto denied = session->Decode<sg::BindPlayerIdentityResponse>(
      chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(denied.size(), 1u);
  EXPECT_EQ(denied[0].code(), AUTH_FAILED);

  // The refused request must not have touched the store.
  auto second = std::make_shared<FakeSession>();
  std::unordered_set<const chirp::network::Session*> empty_trusted;
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(req, second, directory_, &empty_trusted));
  denied = second->Decode<sg::BindPlayerIdentityResponse>(chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(denied.size(), 1u);
  EXPECT_EQ(denied[0].code(), AUTH_FAILED);
}

TEST_F(DispatchTest, ServesTrustedDials) {
  trusted.insert(session.get());
  const auto bound = chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(chirp::gateway::BIND_PLAYER_IDENTITY_REQ,
                 MakeBindRequest("b1", "player-1", "game-a", "u-1"), /*seq=*/77),
      session, directory_, &trusted);
  EXPECT_TRUE(bound);
  ASSERT_EQ(session->sent.size(), 1u);
  // The response echoes the request sequence.
  EXPECT_EQ(session->sent[0].sequence(), 77);

  auto resp =
      session->Decode<sg::BindPlayerIdentityResponse>(chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(resp.size(), 1u);
  EXPECT_EQ(resp[0].code(), OK);
  EXPECT_EQ(resp[0].binding_id(), "b1");

  // The bound record is readable through the block's own RPC.
  sg::GetPlayerIdentitiesRequest get;
  get.set_player_id("player-1");
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(chirp::gateway::GET_PLAYER_IDENTITIES_REQ, get), session, directory_, &trusted));
  auto listed =
      session->Decode<sg::GetPlayerIdentitiesResponse>(chirp::gateway::GET_PLAYER_IDENTITIES_RESP);
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].code(), OK);
  ASSERT_EQ(listed[0].bindings_size(), 1);
  EXPECT_EQ(listed[0].bindings(0).game_id(), "game-a");
}

TEST_F(DispatchTest, MintsSubscriptionIdsForSelfServiceDials) {
  trusted.insert(session.get());
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ,
                 MakeSubscribeRequest("", "player-1", "game-a", "world-1")),
      session, directory_, &trusted));

  auto resp = session->Decode<sg::SubscribePlayerChannelResponse>(
      chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
  ASSERT_EQ(resp.size(), 1u);
  EXPECT_EQ(resp[0].code(), OK);
  EXPECT_FALSE(resp[0].existed());
  EXPECT_TRUE(resp[0].subscription_id().rfind("sub-", 0) == 0);
}

TEST_F(DispatchTest, RejectsMalformedBodies) {
  trusted.insert(session.get());
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::BIND_PLAYER_IDENTITY_REQ);
  pkt.set_sequence(1);
  pkt.set_body("\x01\x02not-a-proto");
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(pkt, session, directory_, &trusted));

  auto resp =
      session->Decode<sg::BindPlayerIdentityResponse>(chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(resp.size(), 1u);
  EXPECT_EQ(resp[0].code(), INVALID_PARAM);
}

TEST_F(DispatchTest, ConsumesUnexpectedResponseIds) {
  // An even id inside the block (a response arriving from a peer) is bogus:
  // consumed with a warning, never fed to a handler and never answered.
  sg::GetUnreadSummaryResponse stray;
  EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(chirp::gateway::GET_UNREAD_SUMMARY_RESP, stray), session, directory_, &trusted));
  EXPECT_TRUE(session->sent.empty());
}

}  // namespace
