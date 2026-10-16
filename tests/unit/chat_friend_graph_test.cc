// 防骚扰好友判定（game_chat_features P2，2255-2258）：FriendGraph 只读消费
// social 平面落在 Redis 的好友快照（chirp:social:friends:<receiver>，值 =
// StoredFriendList）。RedisClient 是虚接口——用一个只实现 Get 的桩直接钉
// 判定矩阵，不起真 Redis。

#include "gtest/gtest.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "network/redis_client.h"
#include "proto/social.pb.h"

#include "friend_graph.h"

namespace chirp {
namespace chat {
namespace {

// 站桩 RedisClient：只回答 Get（按 key 查表）；其余命令不重现（FriendGraph
// 只调 Get）。
class StubRedisClient : public chirp::network::RedisClient {
 public:
  StubRedisClient() : RedisClient("stub", 1) {}

  std::optional<std::string> Get(const std::string& key) override {
    const auto it = kv.find(key);
    return it == kv.end() ? std::nullopt : std::optional<std::string>(it->second);
  }

  std::map<std::string, std::string> kv;
};

std::string StoredFriends(const std::vector<std::string>& ids) {
  chirp::social::StoredFriendList stored;
  for (const auto& id : ids) {
    stored.add_friend_user_ids(id);
  }
  return stored.SerializeAsString();
}

TEST(FriendGraphTest, NullRedisMeansNotFriends) {
  FriendGraph graph(nullptr);
  EXPECT_FALSE(graph.AreFriends("alice", "bob"));
}

TEST(FriendGraphTest, SnapshotHitAndMiss) {
  auto redis = std::make_shared<StubRedisClient>();
  redis->kv["chirp:social:friends:alice"] = StoredFriends({"bob", "carol"});
  FriendGraph graph(redis);
  // sender 出现在 receiver 的快照里 = 好友。
  EXPECT_TRUE(graph.AreFriends("alice", "bob"));
  // 快照里没有的 sender、以及没有快照的 receiver 都按非好友处理。
  EXPECT_FALSE(graph.AreFriends("alice", "dave"));
  EXPECT_FALSE(graph.AreFriends("nobody", "bob"));
}

TEST(FriendGraphTest, CorruptSnapshotMeansNotFriends) {
  auto redis = std::make_shared<StubRedisClient>();
  redis->kv["chirp:social:friends:alice"] = "not-a-protobuf-payload";
  FriendGraph graph(redis);
  // 载荷损坏按非好友收紧（判定只剩同群半边），不抛不崩。
  EXPECT_FALSE(graph.AreFriends("alice", "bob"));
}

TEST(FriendGraphTest, EmptySnapshotMeansNotFriends) {
  auto redis = std::make_shared<StubRedisClient>();
  // social 面会把空好友清单也落盘（清空最后一位好友后）：空清单 = 非好友。
  redis->kv["chirp:social:friends:alice"] = StoredFriends({});
  FriendGraph graph(redis);
  EXPECT_FALSE(graph.AreFriends("alice", "bob"));
}

}  // namespace
}  // namespace chat
}  // namespace chirp
