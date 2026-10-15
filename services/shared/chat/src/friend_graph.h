#ifndef CHIRP_CHAT_FRIEND_GRAPH_H_
#define CHIRP_CHAT_FRIEND_GRAPH_H_

#include <memory>
#include <string>

#include "network/redis_client.h"
#include "proto/social.pb.h"

namespace chirp {
namespace chat {

// 防骚扰好友判定（game_chat_features P2，2255-2258）：读 social 平面持久化
// 的好友快照。key 契约与 services/social/src/main.cc 的 kFriendsPrefix 同源
// （chirp:social:friends:<user_id>，值 = StoredFriendList）——跨服务只读
// 消费，本类绝不回写；两侧常量需同步修改。
//
// 快照语义：好友由 social 面双向落盘（接受请求时双方各写一份），只读
// receiver 一份即可判定。判定为尽力而为——Redis 不可用、key 缺失（social
// 未配 Redis 或从未有过好友）或载荷损坏一律按「非好友」处理，开关的判定
// 只剩同群半边：降级方向是收紧（不误放行陌生人），不会放宽。
//
// 无锁：与 DeliveryPrefs 同一 io 线程契约，发送门在连接线程内调用。
class FriendGraph {
 public:
  // redis 为空（部署未配 --redis_host）时实例仍可用，AreFriends 恒 false。
  explicit FriendGraph(std::shared_ptr<chirp::network::RedisClient> redis);

  // sender 是否在 receiver 的好友快照里。只在接收方开启私聊限制的私聊发送
  // 上调用，无缓存——一次 Get 换判定永远新鲜，快照本身无 TTL。
  bool AreFriends(const std::string& receiver_id, const std::string& sender_id);

 private:
  std::shared_ptr<chirp::network::RedisClient> redis_;
};

}  // namespace chat
}  // namespace chirp

#endif  // CHIRP_CHAT_FRIEND_GRAPH_H_
