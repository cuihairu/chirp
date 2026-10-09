#ifndef CHIRP_CHAT_GROUP_MANAGER_H_
#define CHIRP_CHAT_GROUP_MANAGER_H_

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

  // Create a new group
  std::string CreateGroup(const std::string& creator_id,
                         const std::string& group_name,
                         const std::string& description,
                         const std::string& avatar_url,
                         int32_t max_members,
                         const std::vector<std::string>& initial_members);

  // Get group info
  bool GetGroup(const std::string& group_id, chirp::chat::GroupInfo* info);

  // Add member to group
  bool AddMember(const std::string& group_id, const std::string& user_id,
                chirp::chat::GroupMemberRole role = chirp::chat::MEMBER);

  // Remove member from group
  bool RemoveMember(const std::string& group_id, const std::string& user_id);

  // Get group members
  std::vector<chirp::chat::GroupMember> GetMembers(const std::string& group_id);

  // Check if user is member
  bool IsMember(const std::string& group_id, const std::string& user_id);

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

  std::string GenerateGroupId() {
    static std::atomic<uint64_t> counter{1};
    return "group_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) +
           "_" + std::to_string(counter.fetch_add(1));
  }

  std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<GroupData>> groups_;
  std::unordered_map<std::string, std::unordered_set<std::string>> user_to_groups_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_GROUP_MANAGER_H_
