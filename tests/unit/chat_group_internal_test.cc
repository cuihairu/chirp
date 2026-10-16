// Internal tests for GroupManager: private members are reached through the
// befriended access tag declared in the header; the implementation is
// linked in (no second compilation context).
#include <gtest/gtest.h>

#include <asio.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "group_expiry_sweeper.h"
#include "group_manager.h"

#include "runtime_utils.h"

namespace chirp {
namespace chat {

// Befriended in group_manager.h: provides test-only accessors.
struct GroupManagerInternalAccess {
  static std::mutex& mutex(GroupManager& mgr) { return mgr.mu_; }
  static std::unordered_map<std::string, std::unordered_set<std::string>>&
  user_to_groups(GroupManager& mgr) {
    return mgr.user_to_groups_;
  }
  // 钉住 NowMs() 的墙钟，单测里获得确定的到点裁决；0 恢复墙钟。
  static void set_now_override(GroupManager& mgr, int64_t now_ms) {
    mgr.now_override_ms_.store(now_ms);
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

// ---- 临时频道（P1，2026-10-15）：TTL 存储、惰性摘除、周期清扫 ----

TEST(GroupManagerInternalTest, GroupTtlStoredAndExposed) {
  chirp::chat::GroupManager mgr;
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 10'000);
  const std::string gid = mgr.CreateGroup("alice", "g", "", "", 0, {},
                                           /*expires_at_ms=*/15'000);
  chirp::chat::GroupInfo info;
  ASSERT_TRUE(mgr.GetGroup(gid, &info));
  EXPECT_EQ(info.expires_at(), 15'000);

  auto groups = mgr.GetUserGroups("alice");
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].expires_at(), 15'000);
}

TEST(GroupManagerInternalTest, ExpiredGroupLazilyRemovedOnRead) {
  chirp::chat::GroupManager mgr;
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 10'000);
  const std::string gid =
      mgr.CreateGroup("alice", "g", "", "", 0, {"bob"}, /*expires_at_ms=*/15'000);

  // 未到期：正常可读。
  chirp::chat::GroupInfo info;
  ASSERT_TRUE(mgr.GetGroup(gid, &info));
  EXPECT_EQ(info.expires_at(), 15'000);

  // 到点（expires_at <= NowMs() 即摘除）：所有读路径视同不存在。
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 15'000);
  EXPECT_FALSE(mgr.GetGroup(gid, &info));
  EXPECT_FALSE(mgr.HasGroup(gid));
  EXPECT_FALSE(mgr.IsMember(gid, "alice"));
  EXPECT_TRUE(mgr.GetUserGroups("alice").empty());
  EXPECT_TRUE(mgr.GetMembers(gid).empty());
  {
    std::lock_guard<std::mutex> lock(chirp::chat::GroupManagerInternalAccess::mutex(mgr));
    // 成员映射同步解绑（惰性摘除 + unlink 一次完成）。
    EXPECT_TRUE(chirp::chat::GroupManagerInternalAccess::user_to_groups(mgr).empty());
  }
}

TEST(GroupManagerInternalTest, ShareAnyGroupBasics) {
  chirp::chat::GroupManager mgr;
  // 同群 true；不同群 false；无群 false；本人恒 true（语义自洽）。
  mgr.CreateGroup("alice", "g1", "", "", 0, {"alice", "bob"});
  mgr.CreateGroup("carol", "g2", "", "", 0, {"carol"});
  EXPECT_TRUE(mgr.ShareAnyGroup("alice", "bob"));
  EXPECT_TRUE(mgr.ShareAnyGroup("bob", "alice"));
  EXPECT_FALSE(mgr.ShareAnyGroup("alice", "carol"));
  EXPECT_FALSE(mgr.ShareAnyGroup("alice", "nobody"));
  EXPECT_FALSE(mgr.ShareAnyGroup("nobody", "stranger"));
  EXPECT_TRUE(mgr.ShareAnyGroup("alice", "alice"));
}

TEST(GroupManagerInternalTest, ShareAnyGroupIgnoresExpiredGroup) {
  chirp::chat::GroupManager mgr;
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 10'000);
  mgr.CreateGroup("alice", "g", "", "", 0, {"alice", "bob"}, /*expires_at_ms=*/15'000);
  EXPECT_TRUE(mgr.ShareAnyGroup("alice", "bob"));

  // 到点后共同群被惰性摘除：同群判定随即翻 false。
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 15'000);
  EXPECT_FALSE(mgr.ShareAnyGroup("alice", "bob"));
}

TEST(GroupManagerInternalTest, PermanentGroupNeverExpires) {
  chirp::chat::GroupManager mgr;
  const std::string gid = mgr.CreateGroup("alice", "g", "", "", 0, {});
  chirp::chat::GroupManagerInternalAccess::set_now_override(mgr, 1'000'000'000'000LL);
  EXPECT_TRUE(mgr.HasGroup(gid));
  EXPECT_TRUE(mgr.IsMember(gid, "alice"));
  chirp::chat::GroupInfo info;
  ASSERT_TRUE(mgr.GetGroup(gid, &info));
  EXPECT_EQ(info.expires_at(), 0);
}

TEST(GroupManagerInternalTest, SweepExpiredRemovesDueGroupsAndUnlinksUsers) {
  chirp::chat::GroupManager mgr;
  const std::string temp =
      mgr.CreateGroup("alice", "event", "", "", 0, {"bob"}, /*expires_at_ms=*/20'000);
  const std::string perm = mgr.CreateGroup("alice", "perm", "", "", 0, {"bob"});

  EXPECT_TRUE(mgr.SweepExpired(19'999).empty());

  const auto removed = mgr.SweepExpired(20'000);
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], temp);
  EXPECT_FALSE(mgr.HasGroup(temp));
  EXPECT_TRUE(mgr.HasGroup(perm));

  // 成员映射同步解绑：bob 只剩永久群。
  {
    std::lock_guard<std::mutex> lock(chirp::chat::GroupManagerInternalAccess::mutex(mgr));
    const auto& u2g = chirp::chat::GroupManagerInternalAccess::user_to_groups(mgr);
    ASSERT_EQ(u2g.count("bob"), 1u);
    EXPECT_TRUE(u2g.at("bob").count(perm));
  }
  EXPECT_EQ(mgr.GetUserGroups("bob").size(), 1u);

  // 已摘除的群不再重复出现。
  EXPECT_TRUE(mgr.SweepExpired(30'000).empty());
}

TEST(GroupManagerInternalTest, SweepExpiredToleratesMissingUserMapping) {
  chirp::chat::GroupManager mgr;
  const std::string temp =
      mgr.CreateGroup("alice", "event", "", "", 0, {"bob"}, /*expires_at_ms=*/20'000);

  // 防御分支：成员仍在群里但 user_to_groups_ 反向条目已不在（正常时序不可
  // 达，与 GetUserGroupsSkipsGhostMembership 同类的台账损坏容忍）。摘除照常
  // 完成、不抛不挂。
  {
    std::lock_guard<std::mutex> lock(chirp::chat::GroupManagerInternalAccess::mutex(mgr));
    chirp::chat::GroupManagerInternalAccess::user_to_groups(mgr).erase("bob");
  }
  const auto removed = mgr.SweepExpired(20'000);
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], temp);
  EXPECT_FALSE(mgr.HasGroup(temp));
}

// ---- GroupExpirySweeper（临时频道周期清扫，与保留清扫同款测试形状）----

TEST(GroupExpirySweeperTest, SweepOnceRemovesDueGroupsOnly) {
  asio::io_context io;
  chirp::chat::GroupManager mgr;
  const std::string temp =
      mgr.CreateGroup("alice", "event", "", "", 0, {"bob"}, /*expires_at_ms=*/20'000);
  const std::string perm = mgr.CreateGroup("alice", "perm", "", "", 0, {"bob"});

  chirp::chat::GroupExpirySweeper sweeper(io, mgr);
  EXPECT_TRUE(sweeper.SweepOnce(19'999).empty());
  const auto removed = sweeper.SweepOnce(20'000);
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], temp);
  EXPECT_TRUE(mgr.HasGroup(perm));
  EXPECT_FALSE(mgr.HasGroup(temp));
}

TEST(GroupExpirySweeperTest, TimerLoopSweepsAndStops) {
  asio::io_context io;
  chirp::chat::GroupManager mgr;
  // 到点时刻取墙钟 +60ms（定时器臂传墙钟 now）：10ms 一拍的清扫在第 6 拍
  // 前后命中，run_for(120ms) 有充足余量。
  const std::string temp = mgr.CreateGroup(
      "alice", "event", "", "", 0, {},
      /*expires_at_ms=*/chirp::chat::runtime::NowMs() + 60);
  ASSERT_TRUE(mgr.HasGroup(temp));

  chirp::chat::GroupExpirySweeper sweeper(io, mgr, /*interval_ms=*/10);
  sweeper.Start();
  sweeper.Start();  // 幂等：重复 Start 不挂第二个定时器链。
  (void)io.run_for(std::chrono::milliseconds(120));
  EXPECT_FALSE(mgr.HasGroup(temp));

  sweeper.Stop();
  sweeper.Stop();  // 幂等：重复 Stop 不再 post cancel。
  io.run();        // 排干 strand 上待决的 cancel post（stop 臂不再重排）。
}

// Stop 抢在首次到期处理之前：Start() 先 post 了一次 ScheduleNextRun，
// Stop() 随后清掉 running_ 并把 cancel 排在它后面，io.run() 排干时那条
// ScheduleNextRun 命中 !running_ 早退，链就此停摆（cancel 时定时器还没挂）。
TEST(GroupExpirySweeperTest, StopBeforeFirstTickLeavesChainDead) {
  asio::io_context io;
  chirp::chat::GroupManager mgr;
  // 已过期的临时群：若定时器臂真的跑过一遍，它在此前就被摘除了。
  const std::string temp =
      mgr.CreateGroup("alice", "event", "", "", 0, {},
                      /*expires_at_ms=*/1);
  ASSERT_FALSE(temp.empty());

  chirp::chat::GroupExpirySweeper sweeper(io, mgr, /*interval_ms=*/10);
  sweeper.Start();
  sweeper.Stop();
  io.run();  // 排干：ScheduleNextRun 命中 !running_ 早退，cancel 为空转

  // 链已死：若上面那条 ScheduleNextRun 没早退、把定时器重排上，这段虚拟
  // 时间里的十几次到期处理就会把临时群扫掉。
  io.run_for(std::chrono::milliseconds(200));
  const auto removed = mgr.SweepExpired(1000);
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], temp);
}

}  // namespace
