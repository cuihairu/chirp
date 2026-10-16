#include "friend_graph.h"

#include "logger.h"

namespace chirp {
namespace chat {

namespace {

// 与 services/social/src/main.cc 的 kFriendsPrefix 同源：好友快照 key 前缀
// （值 = chirp::social::StoredFriendList）。跨服务只读消费的契约常量，两侧
// 需同步修改。
constexpr const char* kFriendsPrefix = "chirp:social:friends:";

}  // namespace

FriendGraph::FriendGraph(std::shared_ptr<chirp::network::RedisClient> redis)
    : redis_(std::move(redis)) {}

bool FriendGraph::AreFriends(const std::string& receiver_id,
                             const std::string& sender_id) {
  if (!redis_) {
    return false;
  }
  const auto raw = redis_->Get(std::string(kFriendsPrefix) + receiver_id);
  if (!raw) {
    // 无共享 Redis 或接收方没有快照（social 内存态 / 从未有过好友）：
    // 按「非好友」处理，交由同群半边兜底。
    return false;
  }
  chirp::social::StoredFriendList stored;
  if (!stored.ParseFromString(*raw)) {
    chirp::common::Logger::Instance().Warn(
        "friend_graph: corrupt friend snapshot for " + receiver_id);
    return false;
  }
  for (const auto& id : stored.friend_user_ids()) {
    if (id == sender_id) {
      return true;
    }
  }
  return false;
}

}  // namespace chat
}  // namespace chirp
