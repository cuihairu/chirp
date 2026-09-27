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
  // `publishes` (optional) records every PUBLISH as (channel, payload) so
  // the game-presence event stream is assertable without a server.
  explicit FakeRedisClient(std::shared_ptr<std::map<std::string, std::string>> store,
                           std::shared_ptr<std::vector<std::pair<std::string, std::string>>> publishes =
                               nullptr)
      : RedisClient("127.0.0.1", 1), store_(std::move(store)), publishes_(std::move(publishes)) {}

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
  bool Publish(const std::string& channel, const std::string& message) override {
    if (fail) {
      return false;
    }
    if (publishes_) {
      publishes_->emplace_back(channel, message);
    }
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
  std::shared_ptr<std::vector<std::pair<std::string, std::string>>> publishes_;
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

TEST(IdentityRegistryTest, GetByIdEmptyAndUnknownIdsReturnNull) {
  chat::IdentityRegistry registry;
  // The empty id is the "no by-id lookup requested" sentinel; the unknown
  // id is the ordinary miss. Both hand back null.
  EXPECT_EQ(registry.GetById(""), nullptr);
  EXPECT_EQ(registry.GetById("missing"), nullptr);
}

TEST(IdentityRegistryTest, ResolveGameUserReverseLookup) {
  chat::IdentityRegistry registry;
  ASSERT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.Bind("b2", "player-1", "game-b", "char-9", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);

  auto user = registry.ResolveGameUser("game-a", "player-1");
  ASSERT_NE(user, nullptr);
  EXPECT_EQ(*user, "u-1");
  user = registry.ResolveGameUser("game-b", "player-1");
  ASSERT_NE(user, nullptr);
  EXPECT_EQ(*user, "char-9");

  // Unbound directions: the player holds no identity in that game, and a
  // stranger holds none anywhere.
  EXPECT_EQ(registry.ResolveGameUser("game-c", "player-1"), nullptr);
  EXPECT_EQ(registry.ResolveGameUser("game-a", "stranger"), nullptr);
}

TEST(IdentityRegistryTest, ResolveGameUserIsDeterministicWithMultipleBindings) {
  chat::IdentityRegistry registry;
  // One player, two game users in the same game (distinct tuples, so both
  // stand). The forward index is an unordered_set with no stable order, so
  // the smallest game_user_id must win on every call.
  ASSERT_EQ(registry.Bind("b1", "player-1", "game-a", "u-2", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.Bind("b2", "player-1", "game-a", "u-10", 1001),
            chat::IdentityRegistry::BindOutcome::kBound);
  const auto user = registry.ResolveGameUser("game-a", "player-1");
  ASSERT_NE(user, nullptr);
  EXPECT_EQ(*user, "u-10");  // "u-10" < "u-2"
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
  // detection and is rejected. Player / game / channel each mismatched in
  // turn so every short-circuit arm of the three-way equality runs.
  std::string reused = "s1";
  EXPECT_EQ(registry.Subscribe(&reused, "player-2", "game-a", "world-1", 3000),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  reused = "s1";
  EXPECT_EQ(registry.Subscribe(&reused, "player-1", "game-b", "world-1", 3001),
            chat::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  reused = "s1";
  EXPECT_EQ(registry.Subscribe(&reused, "player-1", "game-a", "world-2", 3002),
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

// ---------------------------------------------------------------------------
// GamePresence (游戏在线状态开关: default-enabled + explicit-choice store)
// ---------------------------------------------------------------------------

TEST(GamePresenceTest, UnknownPlayerReadsAsEnabled) {
  chat::GamePresence presence{chat::GamePresence::RedisFactory()};
  // 绑定即默认开启:从未写过的账号读出来就是 true。
  EXPECT_TRUE(presence.Enabled("nobody"));
  EXPECT_TRUE(presence.Enabled("player-1"));
}

TEST(GamePresenceTest, SetReportsEffectiveChangeAndRoundTrips) {
  chat::GamePresence presence{chat::GamePresence::RedisFactory()};
  // First explicit choice always "changes" the record, even when it agrees
  // with the default - the caller's refresh is idempotent either way.
  EXPECT_TRUE(presence.SetEnabled("player-1", true));
  EXPECT_FALSE(presence.SetEnabled("player-1", true));
  EXPECT_TRUE(presence.SetEnabled("player-1", false));
  EXPECT_FALSE(presence.Enabled("player-1"));
  EXPECT_TRUE(presence.Enabled("player-2"));  // untouched players keep default
  EXPECT_TRUE(presence.SetEnabled("player-1", true));
  EXPECT_TRUE(presence.Enabled("player-1"));
}

TEST(GamePresenceTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::GamePresence writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  writer.Load();  // empty store is a clean no-op
  writer.SetEnabled("player-1", false);
  ASSERT_EQ(store->count("chirp:game_presence:setting:player-1"), 1u);
  EXPECT_EQ((*store)["chirp:game_presence:setting:player-1"], "0");

  // A fresh process replays only the explicit choices.
  chat::GamePresence reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_FALSE(reader.Enabled("player-1"));
  EXPECT_TRUE(reader.Enabled("player-2"));
}

TEST(GamePresenceTest, LoadSkipsCorruptRecords) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  (*store)["chirp:game_presence:setting:player-1"] = "maybe";
  (*store)["chirp:game_presence:setting:player-2"] = "0";
  chat::GamePresence reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  // The unreadable row is skipped (default applies), the readable one lands.
  EXPECT_TRUE(reader.Enabled("player-1"));
  EXPECT_FALSE(reader.Enabled("player-2"));
}

TEST(GamePresenceTest, LoadDefaultsPlayersWhoseRowCannotBeRead) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  (*store)["chirp:game_presence:setting:player-1"] = "0";
  chat::GamePresence reader([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail_get = true;
    return client;
  });
  reader.Load();
  // Keys 列出了行但逐行 Get 失败:该行按默认开启处理,不落 overrides。
  EXPECT_TRUE(reader.Enabled("player-1"));
}

TEST(GamePresenceTest, LoadReplaysExplicitOptIn) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  (*store)["chirp:game_presence:setting:player-1"] = "1";
  chat::GamePresence reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  // 显式开启的选择也要重放:与缺省 true 语义一致,但来源是已落库的选择。
  EXPECT_TRUE(reader.Enabled("player-1"));
}

TEST(GamePresenceTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::GamePresence presence([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  presence.Load();
  EXPECT_TRUE(presence.SetEnabled("player-1", false));
  // The write-through failed but the working authority is memory.
  EXPECT_FALSE(presence.Enabled("player-1"));
  EXPECT_TRUE(store->empty());
}

// ---------------------------------------------------------------------------
// PlayerDirectory game presence: derived roster + pub/sub events (状态不推)
// ---------------------------------------------------------------------------

namespace presence_test {

inline constexpr const char* kRosterPrefix = "chirp:game_presence:online:";
inline constexpr const char* kEvents = "chirp:game_presence:events";

struct Harness {
  std::shared_ptr<std::map<std::string, std::string>> store =
      std::make_shared<std::map<std::string, std::string>>();
  std::shared_ptr<std::vector<std::pair<std::string, std::string>>> publishes =
      std::make_shared<std::vector<std::pair<std::string, std::string>>>();

  chat::PlayerDirectory::Options Options() {
    chat::PlayerDirectory::Options options;
    options.presence_redis = [this]() mutable {
      return std::make_unique<FakeRedisClient>(store, publishes);
    };
    return options;
  }

  // (game_id, online) of every published event, in order.
  std::vector<std::pair<std::string, bool>> Events() const {
    std::vector<std::pair<std::string, bool>> out;
    for (const auto& [channel, payload] : *publishes) {
      EXPECT_EQ(channel, kEvents);
      sg::GamePresenceEvent event;
      EXPECT_TRUE(event.ParseFromString(payload));
      out.emplace_back(event.game_id(), event.online());
    }
    return out;
  }

  // The most recent flip; asserts non-empty so a regression reads as a
  // failure instead of an out-of-range back().
  std::pair<std::string, bool> LastEvent() const {
    const auto events = Events();
    EXPECT_FALSE(events.empty());
    return events.empty() ? std::pair<std::string, bool>{"<none>", false} : events.back();
  }

  std::string Roster(const std::string& player) const {
    const auto it = store->find(std::string(kRosterPrefix) + player);
    return it == store->end() ? std::string("<absent>") : it->second;
  }
};

inline sg::SetGamePresenceEnabledRequest MakeSetRequest(const std::string& player,
                                                        bool enabled) {
  sg::SetGamePresenceEnabledRequest req;
  req.set_player_id(player);
  req.set_enabled(enabled);
  return req;
}

inline sg::GetGamePresenceRequest MakeGetRequest(const std::string& player) {
  sg::GetGamePresenceRequest req;
  req.set_player_id(player);
  return req;
}

}  // namespace presence_test


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

  // A pair that was never bound resolves to no owner: the unbind still
  // answers OK, with nobody to refresh in the game-presence roster.
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_id("game-zz");
              req.set_game_user_id("nobody");
              return req;
            }()).code(),
            OK);

  // binding_id-only is the other valid single-selector shape (by_id true,
  // by_pair false, no half-pair) and takes the by_id log/erase path.
  sg::UnbindPlayerIdentityRequest by_id_only;
  by_id_only.set_binding_id("b9");
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity(by_id_only).code(), OK);

  // by_id != by_pair with malformed_pair: binding_id plus exactly one of the
  // pair fields. This is the only shape that reaches the `malformed_pair`
  // operand of the `||` after `by_id == by_pair` is false.
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_binding_id("b10");
              req.set_game_id("game-a");
              return req;
            }()).code(),
            INVALID_PARAM);
  EXPECT_EQ(directory_.HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_binding_id("b11");
              req.set_game_user_id("u-2");
              return req;
            }()).code(),
            INVALID_PARAM);

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

  // Every proper subset of the triple: malformed_tuple true, by_tuple false.
  sg::UnsubscribePlayerChannelRequest only_player;
  only_player.set_player_id("player-1");
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(only_player).code(), INVALID_PARAM);
  sg::UnsubscribePlayerChannelRequest only_game;
  only_game.set_game_id("game-a");
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(only_game).code(), INVALID_PARAM);
  sg::UnsubscribePlayerChannelRequest only_channel;
  only_channel.set_channel_id("world-1");
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(only_channel).code(), INVALID_PARAM);
  sg::UnsubscribePlayerChannelRequest game_and_channel;
  game_and_channel.set_game_id("game-a");
  game_and_channel.set_channel_id("world-1");
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel(game_and_channel).code(), INVALID_PARAM);

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

  // by_id != by_tuple with malformed_tuple: subscription_id plus a proper
  // non-empty subset of the triple. Only then is `by_id == by_tuple` false
  // and `malformed_tuple` evaluated true.
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel([] {
              sg::UnsubscribePlayerChannelRequest r;
              r.set_subscription_id("s10");
              r.set_player_id("player-1");
              return r;
            }()).code(),
            INVALID_PARAM);
  EXPECT_EQ(directory_.HandleUnsubscribePlayerChannel([] {
              sg::UnsubscribePlayerChannelRequest r;
              r.set_subscription_id("s11");
              r.set_game_id("game-a");
              r.set_channel_id("world-1");
              return r;
            }()).code(),
            INVALID_PARAM);
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
// PlayerDirectory::RelayGameReply (TODO 56: cross-plane reply)
// ---------------------------------------------------------------------------

constexpr auto kNoPrefix = chat::PlayerDirectory::GameReplyOutcome::kNoGamePrefix;
constexpr auto kUnknownGame = chat::PlayerDirectory::GameReplyOutcome::kUnknownGame;
constexpr auto kUnboundPlayer = chat::PlayerDirectory::GameReplyOutcome::kUnboundPlayer;
constexpr auto kSent = chat::PlayerDirectory::GameReplyOutcome::kSent;
constexpr auto kSendFailed = chat::PlayerDirectory::GameReplyOutcome::kSendFailed;

TEST(PlayerDirectoryRelayTest, ChannelWithoutGamePrefixFallsThrough) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  const auto resolve = [](const std::string&) { return std::string("svc-a"); };
  const auto inject = [](const std::string&,
                         const chirp::gateway::PeerInjectMessageNotify&) { return true; };

  // A bare channel, an empty prefix, and an empty bare channel are all
  // ordinary App-side sends (or malformed) — never cross-plane.
  EXPECT_EQ(directory.RelayGameReply("player-1", "world-1", "hi", "", resolve, inject),
            kNoPrefix);
  EXPECT_EQ(directory.RelayGameReply("player-1", ":world", "hi", "", resolve, inject),
            kNoPrefix);
  EXPECT_EQ(directory.RelayGameReply("player-1", "game-a:", "hi", "", resolve, inject),
            kNoPrefix);
}

TEST(PlayerDirectoryRelayTest, UnknownGameRefusesInsteadOfFallingBack) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  // No live spoke for the game: refuse rather than deliver into a local
  // "<game>:<bare>" channel the client would mistake for success.
  EXPECT_EQ(directory.RelayGameReply(
                "player-1", "game-a:world", "hi", "",
                [](const std::string&) { return std::string(); },
                [](const std::string&, const chirp::gateway::PeerInjectMessageNotify&) {
                  return true;
                }),
            kUnknownGame);
}

TEST(PlayerDirectoryRelayTest, PlayerWithoutBindingForThatGameRefuses) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(
      directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(),
      OK);
  // game-b has a live spoke, but the player only holds a game-a identity.
  EXPECT_EQ(directory.RelayGameReply(
                "player-1", "game-b:world", "hi", "",
                [](const std::string&) { return std::string("svc-b"); },
                [](const std::string&, const chirp::gateway::PeerInjectMessageNotify&) {
                  return true;
                }),
            kUnboundPlayer);
}

TEST(PlayerDirectoryRelayTest, SentReplyCarriesGameUserAndBareChannel) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(
      directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(),
      OK);

  std::string sent_service;
  chirp::gateway::PeerInjectMessageNotify sent;
  EXPECT_EQ(directory.RelayGameReply(
                "player-1", "game-a:world-1", "hello", "cm-1",
                [](const std::string& game) {
                  return game == "game-a" ? std::string("svc-a") : std::string();
                },
                [&](const std::string& service_id,
                    const chirp::gateway::PeerInjectMessageNotify& notify) {
                  sent_service = service_id;
                  sent = notify;
                  return true;
                }),
            kSent);
  EXPECT_EQ(sent_service, "svc-a");
  EXPECT_EQ(sent.channel_id(), "world-1");  // bare — the prefix is hub-side naming
  EXPECT_EQ(sent.sender_id(), "u-1");       // the game identity, not the App player
  EXPECT_EQ(sent.content(), "hello");
  EXPECT_EQ(sent.client_msg_id(), "cm-1");
}

TEST(PlayerDirectoryRelayTest, DownlinkRefusalMapsToSendFailed) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(
      directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(),
      OK);
  // The spoke dropped between the resolve and the downlink.
  EXPECT_EQ(directory.RelayGameReply(
                "player-1", "game-a:world", "hi", "",
                [](const std::string&) { return std::string("svc-a"); },
                [](const std::string&, const chirp::gateway::PeerInjectMessageNotify&) {
                  return false;
                }),
            kSendFailed);
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

  // Id one past GET_GAME_PRESENCE_RESP (the block's upper bound since the
  // 游戏在线状态 RPCs landed at 5031-5034): the `id >= BIND && id <=
  // GET_GAME_PRESENCE_RESP` compound takes the second-compare false arm
  // instead of short-circuiting on the first.
  EXPECT_FALSE(chirp::chat::DispatchPlayerDirectoryPacket(
      MakePacket(static_cast<chirp::gateway::MsgID>(5035), unrelated), session, directory_,
      &trusted));
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

TEST_F(DispatchTest, ServesEveryRemainingRequestInTheBlock) {
  // The happy-path dispatch arms for every request id the block routes;
  // BIND/GET_IDENTITIES/SUBSCRIBE have their own tests above.
  trusted.insert(session.get());

  // Seed through the handlers so each RPC answers with real state.
  ASSERT_EQ(directory_
                .HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory_
                .HandleSubscribePlayerChannel(MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  chirp::gateway::ChannelMessageNotify uplink;
  uplink.set_game_id("game-a");
  uplink.set_channel_id("world-1");
  ASSERT_GT(directory_.FanoutChannelMessage(uplink), 0u);

  const auto dispatch = [&](chirp::gateway::MsgID id, const google::protobuf::Message& body) {
    SCOPED_TRACE(static_cast<int>(id));
    EXPECT_TRUE(
        chirp::chat::DispatchPlayerDirectoryPacket(MakePacket(id, body), session, directory_,
                                                   &trusted));
  };

  // RESOLVE_GAME_USER_REQ: the bound identity answers with the player.
  sg::ResolveGameUserRequest resolve;
  resolve.set_game_id("game-a");
  resolve.set_game_user_id("u-1");
  dispatch(chirp::gateway::RESOLVE_GAME_USER_REQ, resolve);
  {
    auto resps = session->Decode<sg::ResolveGameUserResponse>(
        chirp::gateway::RESOLVE_GAME_USER_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
    EXPECT_EQ(resps[0].player_id(), "player-1");
  }

  // GET_PLAYER_SUBSCRIPTIONS_REQ: the seeded subscription comes back.
  sg::GetPlayerSubscriptionsRequest get_subs;
  get_subs.set_player_id("player-1");
  dispatch(chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ, get_subs);
  {
    auto resps = session->Decode<sg::GetPlayerSubscriptionsResponse>(
        chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
    ASSERT_EQ(resps[0].subscriptions_size(), 1);
    EXPECT_EQ(resps[0].subscriptions(0).subscription_id(), "s1");
  }

  // GET_UNREAD_SUMMARY_REQ: one fan-out increment is pending for the player.
  sg::GetUnreadSummaryRequest summary;
  summary.set_player_id("player-1");
  dispatch(chirp::gateway::GET_UNREAD_SUMMARY_REQ, summary);
  {
    auto resps = session->Decode<sg::GetUnreadSummaryResponse>(
        chirp::gateway::GET_UNREAD_SUMMARY_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
    EXPECT_EQ(resps[0].total_unread(), 1);
  }

  // MARK_CHANNELS_READ_REQ: the layered selector clears the seeded counter.
  sg::MarkChannelsReadRequest mark;
  mark.set_player_id("player-1");
  mark.set_game_id("game-a");
  mark.set_channel_id("world-1");
  dispatch(chirp::gateway::MARK_CHANNELS_READ_REQ, mark);
  {
    auto resps = session->Decode<sg::MarkChannelsReadResponse>(
        chirp::gateway::MARK_CHANNELS_READ_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
    EXPECT_EQ(resps[0].cleared(), 1);
  }

  // UNSUBSCRIBE_PLAYER_CHANNEL_REQ: by id, on the seeded subscription.
  sg::UnsubscribePlayerChannelRequest unsub;
  unsub.set_subscription_id("s1");
  dispatch(chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ, unsub);
  {
    auto resps = session->Decode<sg::UnsubscribePlayerChannelResponse>(
        chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
  }

  // UNBIND_PLAYER_IDENTITY_REQ: by binding id, idempotent.
  sg::UnbindPlayerIdentityRequest unbind;
  unbind.set_binding_id("b1");
  dispatch(chirp::gateway::UNBIND_PLAYER_IDENTITY_REQ, unbind);
  {
    auto resps = session->Decode<sg::UnbindPlayerIdentityResponse>(
        chirp::gateway::UNBIND_PLAYER_IDENTITY_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
  }

  // SET_GAME_PRESENCE_ENABLED_REQ: the switch write is served (the binding
  // above was just unbound, so the roster ends up empty either way).
  sg::SetGamePresenceEnabledRequest set_switch;
  set_switch.set_player_id("player-1");
  set_switch.set_enabled(false);
  dispatch(chirp::gateway::SET_GAME_PRESENCE_ENABLED_REQ, set_switch);
  {
    auto resps = session->Decode<sg::SetGamePresenceEnabledResponse>(
        chirp::gateway::SET_GAME_PRESENCE_ENABLED_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
  }

  // GET_GAME_PRESENCE_REQ: reads back the switch we just wrote.
  sg::GetGamePresenceRequest get_presence;
  get_presence.set_player_id("player-1");
  dispatch(chirp::gateway::GET_GAME_PRESENCE_REQ, get_presence);
  {
    auto resps = session->Decode<sg::GetGamePresenceResponse>(
        chirp::gateway::GET_GAME_PRESENCE_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), OK);
    EXPECT_FALSE(resps[0].enabled());
    EXPECT_EQ(resps[0].entries_size(), 0);
  }
}

// ---------------------------------------------------------------------------
// PlayerDirectory::LoadAll (cold-start restore across all three registries)
// ---------------------------------------------------------------------------

TEST(PlayerDirectoryLoadAllTest, RestoresIdentitiesSubscriptionsAndUnread) {
  // One Redis stand-in backing every registry: seed a "previous process"
  // through a writer directory, then restore into a fresh one via LoadAll.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  chat::PlayerDirectory::Options persisted;
  persisted.identities_redis = [store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  };
  persisted.subscriptions_redis = [store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  };
  persisted.unread_redis = [store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  };

  chat::PlayerDirectory writer(persisted);
  ASSERT_EQ(writer.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(writer.HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  chirp::gateway::ChannelMessageNotify uplink;
  uplink.set_game_id("game-a");
  uplink.set_channel_id("world-1");
  ASSERT_GT(writer.FanoutChannelMessage(uplink), 0u);

  chat::PlayerDirectory fresh(persisted);
  fresh.LoadAll();

  sg::GetPlayerIdentitiesRequest get_identities;
  get_identities.set_player_id("player-1");
  const auto identities = fresh.HandleGetPlayerIdentities(get_identities);
  ASSERT_EQ(identities.bindings_size(), 1);
  EXPECT_EQ(identities.bindings(0).binding_id(), "b1");

  sg::GetPlayerSubscriptionsRequest get_subs;
  get_subs.set_player_id("player-1");
  EXPECT_EQ(fresh.HandleGetPlayerSubscriptions(get_subs).subscriptions_size(), 1);

  sg::GetUnreadSummaryRequest summary;
  summary.set_player_id("player-1");
  EXPECT_EQ(fresh.HandleGetUnreadSummary(summary).total_unread(), 1);
}

TEST(PlayerDirectoryLoadAllTest, MemoryOnlyDirectoryLoadsAsACleanNoOp) {
  // Without factories there is nothing to pull: LoadAll must not crash or
  // fabricate state.
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  directory.LoadAll();

  sg::GetPlayerIdentitiesRequest get_identities;
  get_identities.set_player_id("player-1");
  EXPECT_EQ(directory.HandleGetPlayerIdentities(get_identities).bindings_size(), 0);
}


// ---------------------------------------------------------------------------
// Batch D: identity/subscription registry arms + remaining DenyUntrusted
// ---------------------------------------------------------------------------

TEST(IdentityRegistryTest, SameBindingIdReusedForDifferentPlayerFails) {
  // Partial-match idempotency: same binding_id but different player_id only
  // (game/game_user identical) takes the mismatch arm of the three-way compare.
  chat::IdentityRegistry registry;
  ASSERT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(registry.Bind("b1", "player-2", "game-a", "u-1", 2000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  // Same id, different game_id only.
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-b", "u-1", 3000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
  // Same id, different game_user only.
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-2", 4000),
            chat::IdentityRegistry::BindOutcome::kInvalid);
}

TEST(IdentityRegistryTest, ResolveGameUserSkipsOtherGames) {
  // Multi-game player: ResolveGameUser for game-a must continue past the
  // game-b entry (the `game_id != game` skip arm).
  chat::IdentityRegistry registry;
  ASSERT_EQ(registry.Bind("b1", "player-1", "game-a", "u-a", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.Bind("b2", "player-1", "game-b", "u-b", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.Bind("b3", "player-1", "game-c", "u-c", 1000),
            chat::IdentityRegistry::BindOutcome::kBound);

  auto user = registry.ResolveGameUser("game-a", "player-1");
  ASSERT_NE(user, nullptr);
  EXPECT_EQ(*user, "u-a");
  user = registry.ResolveGameUser("game-b", "player-1");
  ASSERT_NE(user, nullptr);
  EXPECT_EQ(*user, "u-b");
}

TEST(SubscriptionRegistryTest, ReSubscribeSameTupleMultipleTimesKeepsId) {
  // Several no-id re-subscribes: each hits the existing-tuple + empty-id arm.
  chat::SubscriptionRegistry registry;
  std::string id;
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  const std::string first = id;
  for (int i = 0; i < 3; ++i) {
    std::string again;
    EXPECT_EQ(registry.Subscribe(&again, "player-1", "game-a", "world-1", 2000 + i),
              chat::SubscriptionRegistry::SubscribeOutcome::kExisted);
    EXPECT_EQ(again, first);
  }
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, GetForPlayerFiltersByNonEmptyGameId) {
  // Non-empty game_id filter takes the second half of the compound if
  // (game_id.empty() || entry.game_id() == game_id).
  chat::SubscriptionRegistry registry;
  std::string a, b;
  ASSERT_EQ(registry.Subscribe(&a, "player-1", "game-a", "ch-1", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  ASSERT_EQ(registry.Subscribe(&b, "player-1", "game-b", "ch-2", 1000),
            chat::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  const auto all = registry.GetForPlayer("player-1", "");
  EXPECT_EQ(all.size(), 2u);
  const auto only_a = registry.GetForPlayer("player-1", "game-a");
  ASSERT_EQ(only_a.size(), 1u);
  EXPECT_EQ(only_a[0].game_id(), "game-a");
  const auto only_b = registry.GetForPlayer("player-1", "game-b");
  ASSERT_EQ(only_b.size(), 1u);
  EXPECT_EQ(only_b[0].game_id(), "game-b");
}

TEST_F(PlayerDirectoryHandlerTest, UnbindLogsThePairPathWhenRemoved) {
  // Pair-based unbind that succeeds takes the log string arm with
  // game_id + ":" + game_user_id (the by_id path is covered elsewhere).
  ASSERT_EQ(directory_
                .HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  sg::UnbindPlayerIdentityRequest req;
  req.set_game_id("game-a");
  req.set_game_user_id("u-1");
  const auto resp = directory_.HandleUnbindPlayerIdentity(req);
  EXPECT_EQ(resp.code(), OK);
}

TEST_F(PlayerDirectoryHandlerTest, UnsubscribeLogsTheTuplePathWhenRemoved) {
  // Tuple-based unsubscribe that succeeds takes the log string arm with
  // player:game:channel (the by_id path is covered elsewhere).
  ASSERT_EQ(directory_
                .HandleSubscribePlayerChannel(
                    MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  sg::UnsubscribePlayerChannelRequest req;
  req.set_player_id("player-1");
  req.set_game_id("game-a");
  req.set_channel_id("world-1");
  const auto resp = directory_.HandleUnsubscribePlayerChannel(req);
  EXPECT_EQ(resp.code(), OK);
}

TEST_F(DispatchTest, DeniesEveryRemainingUntrustedRequest) {
  // BIND was covered in DeniesUntrustedDials; hit the other nine response
  // types so every DenyUntrusted template instantiation runs.
  const auto deny = [&](chirp::gateway::MsgID req_id, const google::protobuf::Message& body,
                        chirp::gateway::MsgID resp_id) {
    session->sent.clear();
    const auto pkt = MakePacket(req_id, body);
    EXPECT_TRUE(chirp::chat::DispatchPlayerDirectoryPacket(pkt, session, directory_, nullptr))
        << static_cast<int>(req_id);
    ASSERT_EQ(session->sent.size(), 1u) << static_cast<int>(req_id);
    EXPECT_EQ(session->sent[0].msg_id(), resp_id);
  };

  sg::UnbindPlayerIdentityRequest unbind;
  unbind.set_binding_id("b1");
  deny(chirp::gateway::UNBIND_PLAYER_IDENTITY_REQ, unbind,
       chirp::gateway::UNBIND_PLAYER_IDENTITY_RESP);
  {
    auto r = session->Decode<sg::UnbindPlayerIdentityResponse>(
        chirp::gateway::UNBIND_PLAYER_IDENTITY_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::GetPlayerIdentitiesRequest get_id;
  get_id.set_player_id("player-1");
  deny(chirp::gateway::GET_PLAYER_IDENTITIES_REQ, get_id,
       chirp::gateway::GET_PLAYER_IDENTITIES_RESP);
  {
    auto r = session->Decode<sg::GetPlayerIdentitiesResponse>(
        chirp::gateway::GET_PLAYER_IDENTITIES_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::ResolveGameUserRequest resolve;
  resolve.set_game_id("game-a");
  resolve.set_game_user_id("u-1");
  deny(chirp::gateway::RESOLVE_GAME_USER_REQ, resolve,
       chirp::gateway::RESOLVE_GAME_USER_RESP);
  {
    auto r = session->Decode<sg::ResolveGameUserResponse>(
        chirp::gateway::RESOLVE_GAME_USER_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  deny(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ,
       MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"),
       chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
  {
    auto r = session->Decode<sg::SubscribePlayerChannelResponse>(
        chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::UnsubscribePlayerChannelRequest unsub;
  unsub.set_subscription_id("s1");
  deny(chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ, unsub,
       chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP);
  {
    auto r = session->Decode<sg::UnsubscribePlayerChannelResponse>(
        chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::GetPlayerSubscriptionsRequest get_subs;
  get_subs.set_player_id("player-1");
  deny(chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ, get_subs,
       chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP);
  {
    auto r = session->Decode<sg::GetPlayerSubscriptionsResponse>(
        chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::MarkChannelsReadRequest mark;
  mark.set_player_id("player-1");
  mark.set_game_id("game-a");
  deny(chirp::gateway::MARK_CHANNELS_READ_REQ, mark,
       chirp::gateway::MARK_CHANNELS_READ_RESP);
  {
    auto r = session->Decode<sg::MarkChannelsReadResponse>(
        chirp::gateway::MARK_CHANNELS_READ_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::GetUnreadSummaryRequest summary;
  summary.set_player_id("player-1");
  deny(chirp::gateway::GET_UNREAD_SUMMARY_REQ, summary,
       chirp::gateway::GET_UNREAD_SUMMARY_RESP);
  {
    auto r = session->Decode<sg::GetUnreadSummaryResponse>(
        chirp::gateway::GET_UNREAD_SUMMARY_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  // The 游戏在线状态 pair rides the same trust gate: an untrusted dial may
  // not flip another player's switch nor read their roster.
  sg::SetGamePresenceEnabledRequest set_switch;
  set_switch.set_player_id("player-1");
  set_switch.set_enabled(false);
  deny(chirp::gateway::SET_GAME_PRESENCE_ENABLED_REQ, set_switch,
       chirp::gateway::SET_GAME_PRESENCE_ENABLED_RESP);
  {
    auto r = session->Decode<sg::SetGamePresenceEnabledResponse>(
        chirp::gateway::SET_GAME_PRESENCE_ENABLED_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }

  session->sent.clear();
  sg::GetGamePresenceRequest get_presence;
  get_presence.set_player_id("player-1");
  deny(chirp::gateway::GET_GAME_PRESENCE_REQ, get_presence,
       chirp::gateway::GET_GAME_PRESENCE_RESP);
  {
    auto r = session->Decode<sg::GetGamePresenceResponse>(
        chirp::gateway::GET_GAME_PRESENCE_RESP);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].code(), AUTH_FAILED);
  }
}


TEST(PlayerDirectoryPresenceTest, BindPublishesOnlineEventAndWritesRoster) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());

  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  // 绑定即上线:一条 online 事件 + roster 键。
  EXPECT_EQ(h.Events(), (std::vector<std::pair<std::string, bool>>{{"game-a", true}}));
  EXPECT_EQ(h.Roster("player-1"), "game-a");

  // A repeated bind (idempotent kExisted) must not re-publish anything.
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  EXPECT_EQ(h.publishes->size(), 1u);
}

TEST(PlayerDirectoryPresenceTest, RosterIsTheSortedGameSet) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());

  // Bind in reverse order: the derived roster and the event stream both
  // follow game_id order, never arrival order.
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  EXPECT_EQ(h.Roster("player-1"), "game-a\ngame-b");
  EXPECT_EQ(h.Events(), (std::vector<std::pair<std::string, bool>>{
                            {"game-b", true}, {"game-a", true}}));
}

TEST(PlayerDirectoryPresenceTest, UnbindTakesThatGameOffline) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);

  sg::UnbindPlayerIdentityRequest unbind;
  unbind.set_binding_id("b1");
  ASSERT_EQ(directory.HandleUnbindPlayerIdentity(unbind).code(), OK);

  // One offline event for the game that went away; the other stays up.
  EXPECT_EQ(h.LastEvent(), (std::pair<std::string, bool>{"game-a", false}));
  EXPECT_EQ(h.Roster("player-1"), "game-b");
}

TEST(PlayerDirectoryPresenceTest, LastUnbindDeletesTheRoster) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);

  sg::UnbindPlayerIdentityRequest unbind;
  unbind.set_game_id("game-a");
  unbind.set_game_user_id("u-1");
  ASSERT_EQ(directory.HandleUnbindPlayerIdentity(unbind).code(), OK);

  // 退出游戏(断言失效)→ 状态自然下线:offline 事件 + roster 键删除。
  EXPECT_EQ(h.Events(), (std::vector<std::pair<std::string, bool>>{
                            {"game-a", true}, {"game-a", false}}));
  EXPECT_EQ(h.Roster("player-1"), "<absent>");
  EXPECT_EQ(h.store->count(std::string(presence_test::kRosterPrefix) + "player-1"), 0u);
}

TEST(PlayerDirectoryPresenceTest, DisablingDropsRosterAndPublishesOfflines) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);

  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", false))
                .code(),
            OK);

  // 关闭态:两个游戏各一条 offline,roster 键删除,GET 报 disabled 且无条目。
  EXPECT_EQ(h.LastEvent(), (std::pair<std::string, bool>{"game-b", false}));
  EXPECT_EQ(h.Roster("player-1"), "<absent>");
  const auto get = directory.HandleGetGamePresence(presence_test::MakeGetRequest("player-1"));
  ASSERT_EQ(get.code(), OK);
  EXPECT_FALSE(get.enabled());
  EXPECT_EQ(get.entries_size(), 0);
}

TEST(PlayerDirectoryPresenceTest, ReEnablingRebuildsFromBindings) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", false))
                .code(),
            OK);
  const size_t before = h.publishes->size();

  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", true))
                .code(),
            OK);
  // 绑定没动,开关重新打开 → 重新上线(重新推 online)。
  EXPECT_EQ(h.LastEvent(), (std::pair<std::string, bool>{"game-a", true}));
  EXPECT_GT(h.publishes->size(), before);
  EXPECT_EQ(h.Roster("player-1"), "game-a");
}

TEST(PlayerDirectoryPresenceTest, BindingsWhileDisabledStayQuiet) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", false))
                .code(),
            OK);

  // A closed switch is the off switch for the push too: binding while
  // disabled publishes nothing and writes no roster.
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  EXPECT_TRUE(h.publishes->empty());
  EXPECT_EQ(h.Roster("player-1"), "<absent>");
}

TEST(PlayerDirectoryPresenceTest, SwitchFlipWithoutChangeRepublishesNothing) {
  presence_test::Harness h;
  chat::PlayerDirectory directory(h.Options());
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  const size_t before = h.publishes->size();
  // Explicitly writing the value the default already gives changes the
  // record but not the derived state: the refresh must stay silent.
  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", true))
                .code(),
            OK);
  EXPECT_EQ(h.publishes->size(), before);
}

TEST(PlayerDirectoryPresenceTest, HandlersValidateEmptyPlayer) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  EXPECT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("", false)).code(),
            INVALID_PARAM);
  EXPECT_EQ(directory.HandleGetGamePresence(presence_test::MakeGetRequest("")).code(),
            INVALID_PARAM);
}

TEST(PlayerDirectoryPresenceTest, GetListsBoundGamesSortedWhileEnabled) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);

  const auto resp = directory.HandleGetGamePresence(presence_test::MakeGetRequest("player-1"));
  ASSERT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.enabled());
  ASSERT_EQ(resp.entries_size(), 2);
  EXPECT_EQ(resp.entries(0).game_id(), "game-a");
  EXPECT_EQ(resp.entries(0).game_user_id(), "u-1");
  EXPECT_EQ(resp.entries(1).game_id(), "game-b");

  // Another player's roster is empty, not an error.
  EXPECT_EQ(directory.HandleGetGamePresence(presence_test::MakeGetRequest("player-9")).entries_size(),
            0);
}

TEST(PlayerDirectoryPresenceTest, LoadAllRestoresSwitchBeforeServing) {
  // Cold start: the persisted explicit choice must be in place before the
  // first bind of that player is handled, or a disabled player would light
  // up again.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  (*store)["chirp:game_presence:setting:player-1"] = "0";

  presence_test::Harness h;
  h.store = store;
  chat::PlayerDirectory directory(h.Options());
  directory.LoadAll();

  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  EXPECT_TRUE(h.publishes->empty());
  EXPECT_FALSE(
      directory.HandleGetGamePresence(presence_test::MakeGetRequest("player-1")).enabled());
}

TEST(PlayerDirectoryPresenceTest, PublishFailureStillAdvancesTheMirrorAndRecovers) {
  // Redis down mid-flip: the event publish and the roster write degrade to
  // a Warn, but the in-memory mirror still advances — otherwise the next
  // successful refresh would replay stale flips (or, worse, drop them).
  auto store = std::make_shared<std::map<std::string, std::string>>();
  auto publishes =
      std::make_shared<std::vector<std::pair<std::string, std::string>>>();
  bool fail = true;
  chat::PlayerDirectory::Options options;
  options.presence_redis = [&]() {
    auto client = std::make_unique<FakeRedisClient>(store, publishes);
    client->fail = fail;
    return client;
  };
  chat::PlayerDirectory directory(options);

  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  // The flip's event was refused and the roster write refused with it.
  EXPECT_TRUE(publishes->empty());
  EXPECT_TRUE(store->empty());

  // Redis returns: the *next* real flip publishes exactly once — the mirror
  // had already advanced to {game-a}, so the unbind emits only the offline.
  fail = false;
  sg::UnbindPlayerIdentityRequest unbind;
  unbind.set_binding_id("b1");
  ASSERT_EQ(directory.HandleUnbindPlayerIdentity(unbind).code(), OK);
  ASSERT_EQ(publishes->size(), 1u);
  sg::GamePresenceEvent event;
  ASSERT_TRUE(event.ParseFromString(publishes->front().second));
  EXPECT_EQ(event.game_id(), "game-a");
  EXPECT_FALSE(event.online());
  // The roster stays deleted: the last binding is gone.
  EXPECT_TRUE(store->find(std::string(presence_test::kRosterPrefix) + "player-1") ==
              store->end());
}

// ---------------------------------------------------------------------------
// PlayerDirectory::RelayFriendMessage (好友私聊投递进游戏)
// ---------------------------------------------------------------------------

struct RelayHarness {
  struct Inject {
    std::string service_id;
    chirp::gateway::PeerInjectMessageNotify notify;
  };
  std::vector<Inject> injected;

  // game_id -> service_id; missing keys resolve to "" (no live spoke).
  chat::PlayerDirectory::GameServiceResolver Resolver(
      std::map<std::string, std::string> spokes) {
    return [spokes = std::move(spokes)](const std::string& game_id) {
      const auto it = spokes.find(game_id);
      return it == spokes.end() ? std::string() : it->second;
    };
  }

  chat::PlayerDirectory::GameReplySender Sender(bool accept_all = true) {
    return [this, accept_all](const std::string& service_id,
                              const chirp::gateway::PeerInjectMessageNotify& notify) {
      injected.push_back({service_id, notify});
      return accept_all;
    };
  }

  const Inject& Only() const {
    EXPECT_EQ(injected.size(), 1u);
    return injected.front();
  }
};

TEST(PlayerDirectoryFriendRelayTest, DeliversOneCopyPerBoundGameInOrder) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);

  RelayHarness h;
  const size_t count = directory.RelayFriendMessage(
      "friend-1", "player-1", "are you online?", "cmi-1",
      h.Resolver({{"game-a", "svc-a"}, {"game-b", "svc-b"}}), h.Sender());

  EXPECT_EQ(count, 2u);
  ASSERT_EQ(h.injected.size(), 2u);
  // Deterministic game_id order, one copy per game.
  EXPECT_EQ(h.injected[0].service_id, "svc-a");
  EXPECT_EQ(h.injected[1].service_id, "svc-b");
  // The spoke addresses the target through channel_id: the bound game
  // identity, while the sender stays the chirp friend (not a game identity).
  EXPECT_EQ(h.injected[0].notify.channel_id(), "u-1");
  EXPECT_EQ(h.injected[1].notify.channel_id(), "u-2");
  EXPECT_EQ(h.injected[0].notify.sender_id(), "friend-1");
  EXPECT_EQ(h.injected[0].notify.content(), "are you online?");
  EXPECT_EQ(h.injected[0].notify.client_msg_id(), "cmi-1");
}

TEST(PlayerDirectoryFriendRelayTest, DisabledRecipientReceivesNothing) {
  // 关闭态回归:开关关掉后,游戏内不再收到好友私聊(常规端照常投递由 chat
  // 主路径负责,这里只断言 relay 归零且一次 inject 都没发生)。
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);

  RelayHarness h;
  const auto resolve = h.Resolver({{"game-a", "svc-a"}});
  const auto send = h.Sender();
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-1", "hi", "", resolve, send), 1u);
  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", false))
                .code(),
            OK);

  h.injected.clear();
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-1", "hi again", "", resolve, send), 0u);
  EXPECT_TRUE(h.injected.empty());

  // Re-opening the switch resumes the relay; the binding never went away.
  ASSERT_EQ(directory.HandleSetGamePresenceEnabled(presence_test::MakeSetRequest("player-1", true))
                .code(),
            OK);
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-1", "back now", "", resolve, send), 1u);
  EXPECT_EQ(h.Only().notify.content(), "back now");
}

TEST(PlayerDirectoryFriendRelayTest, UnboundOrEmptyEndpointsRelayNothing) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  RelayHarness h;
  const auto resolve = h.Resolver({{"game-a", "svc-a"}});
  const auto send = h.Sender();

  EXPECT_EQ(directory.RelayFriendMessage("", "player-1", "hi", "", resolve, send), 0u);
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "", "hi", "", resolve, send), 0u);
  // A player with no bindings is simply not in any game.
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-9", "hi", "", resolve, send), 0u);
  EXPECT_TRUE(h.injected.empty());
}

TEST(PlayerDirectoryFriendRelayTest, MissingSpokeIsASkipNotAnError) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);

  // Only game-a has a live spoke; game-b's backend is offline. The ordinary
  // chat delivery already succeeded, so this is a logged skip.
  RelayHarness h;
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-1", "hi", "",
                                         h.Resolver({{"game-a", "svc-a"}}), h.Sender()),
            1u);
  EXPECT_EQ(h.Only().service_id, "svc-a");
}

TEST(PlayerDirectoryFriendRelayTest, RefusedInjectIsNotCountedButOthersContinue) {
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "u-2"))
                .code(),
            OK);

  // game-a's spoke is unreachable (SendInject false); the second game must
  // still get its copy, and the count only reflects accepted injections.
  int seen = 0;
  const auto sender = [&](const std::string& service_id,
                          const chirp::gateway::PeerInjectMessageNotify& notify) {
    ++seen;
    return service_id != "svc-a";
  };
  EXPECT_EQ(directory.RelayFriendMessage("friend-1", "player-1", "hi", "",
                                         RelayHarness{}.Resolver(
                                             {{"game-a", "svc-a"}, {"game-b", "svc-b"}}),
                                         sender),
            1u);
  EXPECT_EQ(seen, 2);
}

TEST(PlayerDirectoryFriendRelayTest, SenderIsTheChirpFriendNotAGameIdentity) {
  // The relay keeps the App-side identity visible to the game: sender_id is
  // the friend's chirp user id, so the game client can label the message.
  chat::PlayerDirectory directory{chat::PlayerDirectory::Options()};
  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"))
                .code(),
            OK);
  RelayHarness h;
  directory.RelayFriendMessage("player-1", "player-2", "hi", "",
                              h.Resolver({{"game-a", "svc-a"}}), h.Sender());
  // player-2 has no bindings: nothing is injected, and the sender stays the
  // chirp id of whoever asked (never a game user id).
  EXPECT_TRUE(h.injected.empty());

  ASSERT_EQ(directory.HandleBindPlayerIdentity(MakeBindRequest("b2", "player-2", "game-a", "u-2"))
                .code(),
            OK);
  directory.RelayFriendMessage("player-1", "player-2", "hi", "",
                               h.Resolver({{"game-a", "svc-a"}}), h.Sender());
  EXPECT_EQ(h.Only().notify.sender_id(), "player-1");
  EXPECT_EQ(h.Only().notify.channel_id(), "u-2");
}

}  // namespace
