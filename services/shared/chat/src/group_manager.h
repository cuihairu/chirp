#ifndef CHIRP_CHAT_GROUP_MANAGER_H_
#define CHIRP_CHAT_GROUP_MANAGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Group information storage
struct GroupData {
  std::string group_id;
  std::string group_name;
  std::string description;
  std::string avatar_url;
  std::string owner_id;
  int32_t max_members;
  int64_t created_at;
  // 临时频道（P1，2026-10-15）：过期时刻（epoch 毫秒）；0 = 永久群。
  // 到点后群视同不存在——读路径惰性摘除（LookupLocked），另有周期清扫
  // 回收无人访问的过期群（SweepExpired）。
  int64_t expires_at_ms = 0;

  std::unordered_set<std::string> members;
  std::unordered_map<std::string, chirp::chat::GroupMemberRole> member_roles;
  // 群昵称（2122-2124，message_search 批）：成员显示别名；缺省 = 未设置，
  // 渲染回退 username。与群本体同一内存生命周期。
  std::unordered_map<std::string, std::string> member_aliases;
  // 群禁言（2250-2252，game_chat_features P1）：成员禁言截止时刻（epoch
  // 毫秒）；缺省/0 = 未禁言。与群本体同一内存生命周期。
  std::unordered_map<std::string, int64_t> member_mutes;
  std::mutex mu;
};

// Group manager for handling group operations
class GroupManager {
public:
  GroupManager() = default;

  // Create a new group. expires_at_ms = 0 creates a permanent group; a
  // positive value is the epoch-ms instant the group stops existing (the
  // ttl → instant conversion and its 30-day cap live in the handler layer).
  std::string CreateGroup(const std::string& creator_id,
                         const std::string& group_name,
                         const std::string& description,
                         const std::string& avatar_url,
                         int32_t max_members,
                         const std::vector<std::string>& initial_members,
                         int64_t expires_at_ms = 0);

  // 临时频道（P1）：摘除所有到点（expires_at_ms > 0 且 <= now_ms）的群，
  // 返回被摘除的群 id。now_ms 显式传入（定时臂传墙钟，测试传确定值钉裁决）；
  // 无人访问的过期群靠这条回收，读路径的惰性摘除只覆盖被读到的那些。
  std::vector<std::string> SweepExpired(int64_t now_ms);

  // Get group info
  bool GetGroup(const std::string& group_id, chirp::chat::GroupInfo* info);

  // Existence probe without materializing group info
  bool HasGroup(const std::string& group_id);

  // Add member to group
  bool AddMember(const std::string& group_id, const std::string& user_id,
                chirp::chat::GroupMemberRole role = chirp::chat::MEMBER);

  // Remove member from group
  bool RemoveMember(const std::string& group_id, const std::string& user_id);

  // Get group members
  std::vector<chirp::chat::GroupMember> GetMembers(const std::string& group_id);

  // Check if user is member
  bool IsMember(const std::string& group_id, const std::string& user_id);

  // 防骚扰（game_chat_features P2，2255-2258）：两用户是否共享至少一个存活
  // 群（到点临时群经 LookupLocked 惰性摘除后不算）。陌生人私聊门的同群
  // 半边；两人相同恒 true（本人会话本就绕过该门，这里只保证语义自洽）。
  bool ShareAnyGroup(const std::string& user_a, const std::string& user_b);

  // Get user's groups
  std::vector<chirp::chat::GroupInfo> GetUserGroups(const std::string& user_id);

  // Update member role
  bool SetMemberRole(const std::string& group_id, const std::string& user_id,
                    chirp::chat::GroupMemberRole role);

  // 群昵称：设置/清除（空串清除）一名成员的显示别名。群或成员不存在返回
  // false；权限（本人或 MODERATOR+）由 handler 层判定，这里只管存储。
  bool SetMemberAlias(const std::string& group_id, const std::string& user_id,
                      const std::string& alias);

  // 读取一名成员的群昵称；群/成员不存在或未设置返回空串。
  std::string GetMemberAlias(const std::string& group_id, const std::string& user_id);

  // 群禁言：写入/清除（until_ms <= now 或 <= 0 清除）一名成员的禁言截止
  // 时刻。群或成员不存在返回 false；权限（MODERATOR+）由 handler 层判定，
  // 这里只管存储。duration 的上限（30 天）同样在 handler 层。
  bool SetMemberMute(const std::string& group_id, const std::string& user_id,
                     int64_t until_ms);

  // 读取一名成员的禁言截止时刻；未禁言、已到点（惰性过期：读到过期项顺手
  // 删除）或群/成员不存在返回 0。
  int64_t MutedUntil(const std::string& group_id, const std::string& user_id,
                     int64_t now_ms);

  // alias 的服务端上限（码点数，与 chat_validation 的码点计数同口径）；
  // 超限 handler 层回 INVALID_PARAM。
  static constexpr size_t kMaxAliasCodePoints = 64;

private:
  // Test access: internal tests befriend this tag to reach private state
  // without changing the compiled token stream.
  friend struct GroupManagerInternalAccess;

  // Wall clock in epoch ms, or the test override when set (0 = wall clock).
  // 临时频道的到点语义在单测里需要确定性，生产路径永远走墙钟。
  int64_t NowMs() const;

  // 台账查找 + 临时频道的惰性摘除：命中到点群（expires_at_ms > 0 且
  // <= NowMs()）视为不存在，顺手从 groups_/user_to_groups_ 摘除并返回
  // 空。调用方必须持有 mu_。返回的 shared_ptr 与台账解耦（摘除不影响
  // 其他线程仍持有的引用）。
  std::shared_ptr<GroupData> LookupLocked(const std::string& group_id);

  // 摘除一群在 user_to_groups_ 里的反向引用（调用方持有 mu_ 且 group 是
  // shared_ptr 副本，摘除不影响对象存活）。惰性摘除与周期清扫共用。
  void UnlinkGroupLocked(const std::string& group_id,
                         const std::shared_ptr<GroupData>& group);

  std::string GenerateGroupId() {
    static std::atomic<uint64_t> counter{1};
    return "group_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) +
           "_" + std::to_string(counter.fetch_add(1));
  }

  std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<GroupData>> groups_;
  std::unordered_map<std::string, std::unordered_set<std::string>> user_to_groups_;
  // 测试接缝：非 0 时 NowMs() 直接返回该值（单测钉临时频道到点裁决）。
  std::atomic<int64_t> now_override_ms_{0};
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_GROUP_MANAGER_H_
