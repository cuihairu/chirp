#include "group_manager.h"

#include <algorithm>

namespace chirp {
namespace chat {

std::string GroupManager::CreateGroup(const std::string& creator_id,
                                     const std::string& group_name,
                                     const std::string& description,
                                     const std::string& avatar_url,
                                     int32_t max_members,
                                     const std::vector<std::string>& initial_members) {
  auto group = std::make_shared<GroupData>();
  group->group_id = GenerateGroupId();
  group->group_name = group_name;
  group->description = description;
  group->avatar_url = avatar_url;
  group->owner_id = creator_id;
  group->max_members = max_members;
  group->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  // Add owner as admin
  group->members.insert(creator_id);
  group->member_roles[creator_id] = chirp::chat::ADMIN;

  // Add initial members
  for (const auto& member_id : initial_members) {
    if (member_id != creator_id) {
      group->members.insert(member_id);
      group->member_roles[member_id] = chirp::chat::MEMBER;
    }
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    groups_[group->group_id] = group;

    // Update user to groups mapping
    for (const auto& member_id : group->members) {
      user_to_groups_[member_id].insert(group->group_id);
    }
  }

  return group->group_id;
}

bool GroupManager::GetGroup(const std::string& group_id, chirp::chat::GroupInfo* info) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return false;
  }

  const auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);

  info->set_group_id(group->group_id);
  info->set_group_name(group->group_name);
  info->set_description(group->description);
  info->set_avatar_url(group->avatar_url);
  info->set_owner_id(group->owner_id);
  info->set_member_count(static_cast<int32_t>(group->members.size()));
  info->set_max_members(group->max_members);
  info->set_created_at(group->created_at);

  return true;
}

bool GroupManager::AddMember(const std::string& group_id, const std::string& user_id,
                            chirp::chat::GroupMemberRole role) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return false;
  }

  auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);

  if (group->max_members > 0 && static_cast<int32_t>(group->members.size()) >= group->max_members) {
    return false;
  }

  group->members.insert(user_id);
  group->member_roles[user_id] = role;
  user_to_groups_[user_id].insert(group_id);

  return true;
}

bool GroupManager::RemoveMember(const std::string& group_id, const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return false;
  }

  auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);

  group->members.erase(user_id);
  group->member_roles.erase(user_id);

  auto user_it = user_to_groups_.find(user_id);
  if (user_it != user_to_groups_.end()) {
    user_it->second.erase(group_id);
    if (user_it->second.empty()) {
      user_to_groups_.erase(user_it);
    }
  }

  return true;
}

std::vector<chirp::chat::GroupMember> GroupManager::GetMembers(const std::string& group_id) {
  std::vector<chirp::chat::GroupMember> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return result;
  }

  const auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);

  result.reserve(group->members.size());
  for (const auto& member_id : group->members) {
    chirp::chat::GroupMember member;
    member.set_user_id(member_id);
    // Note: username would come from user service in production
    member.set_role(group->member_roles[member_id]);
    member.set_joined_at(group->created_at);  // Simplified
    result.push_back(std::move(member));
  }

  return result;
}

bool GroupManager::IsMember(const std::string& group_id, const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return false;
  }

  const auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);
  return group->members.count(user_id) > 0;
}

std::vector<chirp::chat::GroupInfo> GroupManager::GetUserGroups(const std::string& user_id) {
  std::vector<chirp::chat::GroupInfo> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto user_it = user_to_groups_.find(user_id);
  if (user_it == user_to_groups_.end()) {
    return result;
  }

  for (const auto& group_id : user_it->second) {
    auto group_it = groups_.find(group_id);
    if (group_it != groups_.end()) {
      chirp::chat::GroupInfo info;
      if (GetGroup(group_id, &info)) {
        result.push_back(std::move(info));
      }
    }
  }

  return result;
}

bool GroupManager::SetMemberRole(const std::string& group_id, const std::string& user_id,
                                chirp::chat::GroupMemberRole role) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return false;
  }

  auto& group = it->second;
  std::lock_guard<std::mutex> group_lock(group->mu);

  if (group->members.count(user_id) == 0) {
    return false;
  }

  group->member_roles[user_id] = role;
  return true;
}

} // namespace chat
} // namespace chirp
