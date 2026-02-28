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

private:
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
