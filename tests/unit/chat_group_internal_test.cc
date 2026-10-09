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

TEST(GroupManagerInternalTest, MemberMuteRoundTripAndLazyExpiry) {
  chirp::chat::GroupManager mgr;
  const std::string gid = mgr.CreateGroup("alice", "g", "", "", 0, {"bob"});

  // handler 层会先挡掉群不存在/成员不存在，这两个防御分支只可能从
  // GroupManager 直达。
  EXPECT_FALSE(mgr.SetMemberMute("ghost-group", "bob", 1000));
  EXPECT_FALSE(mgr.SetMemberMute(gid, "stranger", 1000));

  // 未禁言/未知群/未知成员：0。
  EXPECT_EQ(mgr.MutedUntil(gid, "bob", 0), 0);
  EXPECT_EQ(mgr.MutedUntil("ghost-group", "bob", 0), 0);
  EXPECT_EQ(mgr.MutedUntil(gid, "stranger", 0), 0);

  // 写入后未到期可读回；到点即惰性过期（读到 0 且条目被清，再读仍 0）。
  EXPECT_TRUE(mgr.SetMemberMute(gid, "bob", 5'000));
  EXPECT_EQ(mgr.MutedUntil(gid, "bob", 4'999), 5'000);
  EXPECT_EQ(mgr.MutedUntil(gid, "bob", 5'000), 0);
  EXPECT_EQ(mgr.MutedUntil(gid, "bob", 6'000), 0);

  // 解禁（until_ms <= 0 清除）：清除后再读 0。
  EXPECT_TRUE(mgr.SetMemberMute(gid, "bob", 9'000));
  EXPECT_TRUE(mgr.SetMemberMute(gid, "bob", 0));
  EXPECT_EQ(mgr.MutedUntil(gid, "bob", 9'000), 0);

  // GetMembers 随成员列表下发 muted_until_ts；过期项不出现（惰性清理已移除），
  // 在禁项原值下发。
  EXPECT_TRUE(mgr.SetMemberMute(gid, "bob", 20'000));
  bool seen = false;
  for (const auto& member : mgr.GetMembers(gid)) {
    if (member.user_id() == "bob") {
      seen = true;
      EXPECT_EQ(member.muted_until_ts(), 20'000);
    }
  }
  EXPECT_TRUE(seen);
}

}  // namespace
