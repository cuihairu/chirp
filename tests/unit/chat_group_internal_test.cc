// Internal tests for GroupManager: private members are reached through the
// befriended access tag declared in the header; the implementation is
// linked in (no second compilation context).
#include <gtest/gtest.h>

#include <mutex>
#include <string>

#include "group_manager.h"

namespace chirp {
namespace chat {

// Befriended in group_manager.h: provides test-only accessors.
struct GroupManagerInternalAccess {
  static std::mutex& mutex(GroupManager& mgr) { return mgr.mu_; }
  static std::unordered_map<std::string, std::unordered_set<std::string>>&
  user_to_groups(GroupManager& mgr) {
    return mgr.user_to_groups_;
  }
};

}  // namespace chat
}  // namespace chirp

namespace {

TEST(GroupManagerInternalTest, GetUserGroupsSkipsGhostMembership) {
  chirp::chat::GroupManager mgr;
  const std::string gid = mgr.CreateGroup("alice", "g", "", "", 0, {});

  // Register a membership for a group id that is not present in groups_
  // (the defensive lookup-miss branch must skip it silently).
  {
    auto& mgr_ref = mgr;
    std::lock_guard<std::mutex> lock(chirp::chat::GroupManagerInternalAccess::mutex(mgr_ref));
    chirp::chat::GroupManagerInternalAccess::user_to_groups(mgr_ref)["bob"].insert("ghost-group");
  }

  EXPECT_TRUE(mgr.GetUserGroups("bob").empty());

  auto alice_groups = mgr.GetUserGroups("alice");
  ASSERT_EQ(alice_groups.size(), 1u);
  EXPECT_EQ(alice_groups[0].group_id(), gid);
}

TEST(GroupManagerInternalTest, SetMemberAliasRejectsUnknownGroupAndMember) {
  chirp::chat::GroupManager mgr;
  const std::string gid = mgr.CreateGroup("alice", "g", "", "", 0, {});

  // handler 层会先挡掉群不存在/成员不存在，这两个防御分支只可能从
  // GroupManager 直达。
  EXPECT_FALSE(mgr.SetMemberAlias("ghost-group", "alice", "x"));
  EXPECT_FALSE(mgr.SetMemberAlias(gid, "stranger", "x"));
  EXPECT_TRUE(mgr.SetMemberAlias(gid, "alice", "x"));
  EXPECT_EQ(mgr.GetMemberAlias(gid, "alice"), "x");

  // 未知群/未设置成员的读取分支：空串。
  EXPECT_TRUE(mgr.GetMemberAlias("ghost-group", "alice").empty());
  EXPECT_TRUE(mgr.GetMemberAlias(gid, "stranger").empty());
}

}  // namespace
