#include "group_manager.h"

#include <algorithm>
#include <chrono>

namespace chirp {
namespace chat {

std::string GroupManager::CreateGroup(const std::string& creator_id,
                                     const std::string& group_name,
                                     const std::string& description,
                                     const std::string& avatar_url,
                                     int32_t max_members,
                                     const std::vector<std::string>& initial_members,
                                     int64_t expires_at_ms) {
  auto group = std::make_shared<GroupData>();
  group->group_id = GenerateGroupId();
  group->group_name = group_name;
  group->description = description;
  group->avatar_url = avatar_url;
  group->owner_id = creator_id;
  group->max_members = max_members;
  group->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  group->expires_at_ms = expires_at_ms;

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

bool GroupManager::HasGroup(const std::string& group_id) {
  std::lock_guard<std::mutex> lock(mu_);
  return LookupLocked(group_id) != nullptr;
}

bool GroupManager::GetGroup(const std::string& group_id, chirp::chat::GroupInfo* info) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  info->set_group_id(group->group_id);
  info->set_group_name(group->group_name);
  info->set_description(group->description);
  info->set_avatar_url(group->avatar_url);
  info->set_owner_id(group->owner_id);
  info->set_member_count(static_cast<int32_t>(group->members.size()));
  info->set_max_members(group->max_members);
  info->set_created_at(group->created_at);
  info->set_expires_at(group->expires_at_ms);

  return true;
}

bool GroupManager::AddMember(const std::string& group_id, const std::string& user_id,
                            chirp::chat::GroupMemberRole role) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
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
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  group->members.erase(user_id);
  group->member_roles.erase(user_id);
  group->member_aliases.erase(user_id);

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
  auto group = LookupLocked(group_id);
  if (!group) {
    return result;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  result.reserve(group->members.size());
  for (const auto& member_id : group->members) {
    chirp::chat::GroupMember member;
    member.set_user_id(member_id);
    // Note: username would come from user service in production
    member.set_role(group->member_roles[member_id]);
    member.set_joined_at(group->created_at);  // Simplified
    const auto alias_it = group->member_aliases.find(member_id);
    if (alias_it != group->member_aliases.end()) {
      member.set_alias(alias_it->second);
    }
    const auto mute_it = group->member_mutes.find(member_id);
    if (mute_it != group->member_mutes.end()) {
      member.set_muted_until_ts(mute_it->second);
    }
    result.push_back(std::move(member));
  }

  return result;
}

bool GroupManager::IsMember(const std::string& group_id, const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);
  return group->members.count(user_id) > 0;
}

bool GroupManager::ShareAnyGroup(const std::string& user_a,
                                 const std::string& user_b) {
  if (user_a == user_b) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto a_it = user_to_groups_.find(user_a);
  const auto b_it = user_to_groups_.find(user_b);
  if (a_it == user_to_groups_.end() || b_it == user_to_groups_.end()) {
    return false;
  }
  // 从较小一侧的群集合出发逐个走 LookupLocked：命中到点群会顺手被惰性
  // 摘除，不影响其余判定（与 IsMember 的读路径同契约）。
  const auto& [smaller, larger] =
      a_it->second.size() <= b_it->second.size() ? std::pair{a_it->second, b_it->second}
                                                 : std::pair{b_it->second, a_it->second};
  for (const auto& group_id : smaller) {
    if (larger.count(group_id) > 0 && LookupLocked(group_id) != nullptr) {
      return true;
    }
  }
  return false;
}

std::vector<chirp::chat::GroupInfo> GroupManager::GetUserGroups(const std::string& user_id) {
  std::vector<chirp::chat::GroupInfo> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto user_it = user_to_groups_.find(user_id);
  if (user_it == user_to_groups_.end()) {
    return result;
  }

  // NOTE: GetGroup() also locks mu_; call it while holding mu_ would
  // deadlock (std::mutex is not recursive), so fill the info inline here.
  for (const auto& group_id : user_it->second) {
    auto group = LookupLocked(group_id);
    if (!group) {
      continue;
    }
    std::lock_guard<std::mutex> group_lock(group->mu);

    chirp::chat::GroupInfo info;
    info.set_group_id(group->group_id);
    info.set_group_name(group->group_name);
    info.set_description(group->description);
    info.set_avatar_url(group->avatar_url);
    info.set_owner_id(group->owner_id);
    info.set_member_count(static_cast<int32_t>(group->members.size()));
    info.set_max_members(group->max_members);
    info.set_created_at(group->created_at);
    info.set_expires_at(group->expires_at_ms);
    result.push_back(std::move(info));
  }

  return result;
}

bool GroupManager::SetMemberRole(const std::string& group_id, const std::string& user_id,
                                chirp::chat::GroupMemberRole role) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  if (group->members.count(user_id) == 0) {
    return false;
  }

  group->member_roles[user_id] = role;
  return true;
}

bool GroupManager::SetMemberAlias(const std::string& group_id, const std::string& user_id,
                                  const std::string& alias) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  if (group->members.count(user_id) == 0) {
    return false;
  }

  if (alias.empty()) {
    group->member_aliases.erase(user_id);
  } else {
    group->member_aliases[user_id] = alias;
  }
  return true;
}

std::string GroupManager::GetMemberAlias(const std::string& group_id,
                                         const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return {};
  }
  std::lock_guard<std::mutex> group_lock(group->mu);
  auto alias_it = group->member_aliases.find(user_id);
  return alias_it == group->member_aliases.end() ? std::string() : alias_it->second;
}

bool GroupManager::SetMemberMute(const std::string& group_id,
                                 const std::string& user_id, int64_t until_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return false;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);

  if (group->members.count(user_id) == 0) {
    return false;
  }

  if (until_ms <= 0) {
    group->member_mutes.erase(user_id);
  } else {
    group->member_mutes[user_id] = until_ms;
  }
  return true;
}

int64_t GroupManager::MutedUntil(const std::string& group_id,
                                 const std::string& user_id, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto group = LookupLocked(group_id);
  if (!group) {
    return 0;
  }
  std::lock_guard<std::mutex> group_lock(group->mu);
  auto mute_it = group->member_mutes.find(user_id);
  if (mute_it == group->member_mutes.end()) {
    return 0;
  }
  if (mute_it->second <= now_ms) {
    // 惰性过期：到点后的第一次读取清掉过期项，返回未禁言。
    group->member_mutes.erase(mute_it);
    return 0;
  }
  return mute_it->second;
}

int64_t GroupManager::NowMs() const {
  const int64_t override_ms = now_override_ms_.load();
  if (override_ms != 0) {
    return override_ms;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

void GroupManager::UnlinkGroupLocked(const std::string& group_id,
                                     const std::shared_ptr<GroupData>& group) {
  std::lock_guard<std::mutex> group_lock(group->mu);
  for (const auto& member_id : group->members) {
    auto user_it = user_to_groups_.find(member_id);
    if (user_it == user_to_groups_.end()) {
      continue;
    }
    user_it->second.erase(group_id);
    if (user_it->second.empty()) {
      user_to_groups_.erase(user_it);
    }
  }
}

std::shared_ptr<GroupData> GroupManager::LookupLocked(const std::string& group_id) {
  auto it = groups_.find(group_id);
  if (it == groups_.end()) {
    return nullptr;
  }
  auto group = it->second;
  if (group->expires_at_ms > 0 && group->expires_at_ms <= NowMs()) {
    // 惰性摘除：到点群读到即清，成员映射同步解绑。
    UnlinkGroupLocked(group_id, group);
    groups_.erase(it);
    return nullptr;
  }
  return group;
}

std::vector<std::string> GroupManager::SweepExpired(int64_t now_ms) {
  std::vector<std::string> removed;

  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = groups_.begin(); it != groups_.end();) {
    auto group = it->second;  // shared_ptr copy: keeps the group alive across erase
    if (group->expires_at_ms <= 0 || group->expires_at_ms > now_ms) {
      ++it;
      continue;
    }
    UnlinkGroupLocked(it->first, group);
    removed.push_back(it->first);
    it = groups_.erase(it);
  }
  return removed;
}

} // namespace chat
} // namespace chirp
