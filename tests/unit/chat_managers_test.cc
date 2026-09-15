#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "group_manager.h"
#include "network/protobuf_framing.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "read_receipt_manager.h"
#include "reaction_manager.h"
#include "runtime_utils.h"
#include "typing_manager.h"

using chirp::chat::GroupInfo;
using chirp::chat::GroupManager;
using chirp::chat::GroupMember;
using chirp::chat::ADMIN;
using chirp::chat::MEMBER;
using chirp::chat::MODERATOR;

namespace {

GroupMember FindMember(const std::vector<GroupMember>& members,
                       const std::string& user_id) {
  for (const auto& m : members) {
    if (m.user_id() == user_id) {
      return m;
    }
  }
  return GroupMember{};
}

class GroupManagerTest : public ::testing::Test {
protected:
  GroupManager mgr_;
};

TEST_F(GroupManagerTest, CreateGroupReturnsUniqueId) {
  std::string id1 = mgr_.CreateGroup("alice", "G1", "d1", "a.png", 10, {});
  std::string id2 = mgr_.CreateGroup("alice", "G2", "d2", "a.png", 10, {});

  EXPECT_FALSE(id1.empty());
  EXPECT_FALSE(id2.empty());
  EXPECT_NE(id1, id2);
  EXPECT_EQ(id1.substr(0, 6), "group_");
}

TEST_F(GroupManagerTest, CreateGroupOwnerIsAdmin) {
  std::string gid = mgr_.CreateGroup("alice", "G1", "desc", "avatar", 50, {});

  auto members = mgr_.GetMembers(gid);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0].user_id(), "alice");
  EXPECT_EQ(members[0].role(), ADMIN);
}

TEST_F(GroupManagerTest, CreateGroupWithInitialMembers) {
  std::vector<std::string> initial = {"bob", "carol"};
  std::string gid = mgr_.CreateGroup("alice", "G1", "", "", 10, initial);

  auto members = mgr_.GetMembers(gid);
  EXPECT_EQ(members.size(), 3u);

  EXPECT_EQ(FindMember(members, "alice").role(), ADMIN);
  EXPECT_EQ(FindMember(members, "bob").role(), MEMBER);
  EXPECT_EQ(FindMember(members, "carol").role(), MEMBER);
}

TEST_F(GroupManagerTest, CreateGroupIgnoresDuplicateCreatorInInitialMembers) {
  std::vector<std::string> initial = {"alice", "bob"};
  std::string gid = mgr_.CreateGroup("alice", "G1", "", "", 10, initial);

  auto members = mgr_.GetMembers(gid);
  EXPECT_EQ(members.size(), 2u);
  EXPECT_EQ(FindMember(members, "alice").role(), ADMIN);
}

TEST_F(GroupManagerTest, GetGroupPopulatesAllFields) {
  std::string gid =
      mgr_.CreateGroup("alice", "Team", "a team", "http://avatar", 42, {"bob"});

  GroupInfo info;
  ASSERT_TRUE(mgr_.GetGroup(gid, &info));

  EXPECT_EQ(info.group_id(), gid);
  EXPECT_EQ(info.group_name(), "Team");
  EXPECT_EQ(info.description(), "a team");
  EXPECT_EQ(info.avatar_url(), "http://avatar");
  EXPECT_EQ(info.owner_id(), "alice");
  EXPECT_EQ(info.member_count(), 2);
  EXPECT_EQ(info.max_members(), 42);
  EXPECT_GT(info.created_at(), 0);
}

TEST_F(GroupManagerTest, GetGroupMissingReturnsFalse) {
  GroupInfo info;
  EXPECT_FALSE(mgr_.GetGroup("no_such_group", &info));
}

TEST_F(GroupManagerTest, AddMemberDefaultRoleMember) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {});
  ASSERT_TRUE(mgr_.AddMember(gid, "bob"));

  EXPECT_TRUE(mgr_.IsMember(gid, "bob"));
  auto members = mgr_.GetMembers(gid);
  EXPECT_EQ(FindMember(members, "bob").role(), MEMBER);
}

TEST_F(GroupManagerTest, AddMemberWithExplicitRole) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {});
  ASSERT_TRUE(mgr_.AddMember(gid, "bob", MODERATOR));

  auto members = mgr_.GetMembers(gid);
  EXPECT_EQ(FindMember(members, "bob").role(), MODERATOR);
}

TEST_F(GroupManagerTest, AddMemberToMissingGroupFails) {
  EXPECT_FALSE(mgr_.AddMember("missing", "bob"));
}

TEST_F(GroupManagerTest, AddMemberEnforcesMaxMembers) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 2, {});
  EXPECT_TRUE(mgr_.AddMember(gid, "bob"));   // 2 members now
  EXPECT_FALSE(mgr_.AddMember(gid, "carol"));  // exceeds cap
}

TEST_F(GroupManagerTest, AddMemberNoLimitWhenMaxZero) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 0, {});
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(mgr_.AddMember(gid, "user" + std::to_string(i)));
  }
  EXPECT_EQ(mgr_.GetMembers(gid).size(), 21u);
}

TEST_F(GroupManagerTest, AddExistingMemberIsIdempotent) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {"bob"});
  EXPECT_TRUE(mgr_.AddMember(gid, "bob"));
  EXPECT_EQ(mgr_.GetMembers(gid).size(), 2u);
}

TEST_F(GroupManagerTest, RemoveMemberDeletesMembershipAndRole) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {"bob"});

  ASSERT_TRUE(mgr_.RemoveMember(gid, "bob"));
  EXPECT_FALSE(mgr_.IsMember(gid, "bob"));
  EXPECT_EQ(mgr_.GetMembers(gid).size(), 1u);
}

TEST_F(GroupManagerTest, RemoveMemberFromMissingGroupFails) {
  EXPECT_FALSE(mgr_.RemoveMember("missing", "bob"));
}

TEST_F(GroupManagerTest, RemoveMissingMemberStillReturnsTrue) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {});
  EXPECT_TRUE(mgr_.RemoveMember(gid, "ghost"));
}

TEST_F(GroupManagerTest, IsMemberMissingGroupReturnsFalse) {
  EXPECT_FALSE(mgr_.IsMember("missing", "alice"));
}

TEST_F(GroupManagerTest, GetMembersMissingGroupReturnsEmpty) {
  EXPECT_TRUE(mgr_.GetMembers("missing").empty());
}

TEST_F(GroupManagerTest, MemberFieldsPopulated) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {"bob"});
  auto members = mgr_.GetMembers(gid);

  const auto& bob = FindMember(members, "bob");
  EXPECT_EQ(bob.user_id(), "bob");
  EXPECT_GT(bob.joined_at(), 0);
}

TEST_F(GroupManagerTest, SetMemberRole) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {"bob"});

  ASSERT_TRUE(mgr_.SetMemberRole(gid, "bob", ADMIN));
  EXPECT_EQ(FindMember(mgr_.GetMembers(gid), "bob").role(), ADMIN);

  ASSERT_TRUE(mgr_.SetMemberRole(gid, "bob", MEMBER));
  EXPECT_EQ(FindMember(mgr_.GetMembers(gid), "bob").role(), MEMBER);
}

TEST_F(GroupManagerTest, SetMemberRoleForMissingGroupFails) {
  EXPECT_FALSE(mgr_.SetMemberRole("missing", "bob", ADMIN));
}

TEST_F(GroupManagerTest, SetMemberRoleForNonMemberFails) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {});
  EXPECT_FALSE(mgr_.SetMemberRole(gid, "ghost", ADMIN));
}

TEST_F(GroupManagerTest, GetUserGroupsListsAllMemberships) {
  std::string g1 = mgr_.CreateGroup("alice", "G1", "", "", 10, {"bob"});
  std::string g2 = mgr_.CreateGroup("carol", "G2", "", "", 10, {"bob"});

  auto groups = mgr_.GetUserGroups("bob");
  ASSERT_EQ(groups.size(), 2u);

  bool found_g1 = false, found_g2 = false;
  for (const auto& info : groups) {
    if (info.group_id() == g1) {
      found_g1 = true;
      EXPECT_EQ(info.group_name(), "G1");
      EXPECT_EQ(info.owner_id(), "alice");
      EXPECT_EQ(info.member_count(), 2);
    }
    if (info.group_id() == g2) {
      found_g2 = true;
      EXPECT_EQ(info.group_name(), "G2");
    }
  }
  EXPECT_TRUE(found_g1);
  EXPECT_TRUE(found_g2);
}

TEST_F(GroupManagerTest, GetUserGroupsUnknownUserReturnsEmpty) {
  mgr_.CreateGroup("alice", "G", "", "", 10, {});
  EXPECT_TRUE(mgr_.GetUserGroups("nobody").empty());
}

TEST_F(GroupManagerTest, RemovedMemberNoLongerInUserGroups) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 10, {"bob"});
  ASSERT_EQ(mgr_.GetUserGroups("bob").size(), 1u);

  ASSERT_TRUE(mgr_.RemoveMember(gid, "bob"));
  EXPECT_TRUE(mgr_.GetUserGroups("bob").empty());
}

TEST_F(GroupManagerTest, ConcurrentOperationsDoNotDeadlock) {
  std::string gid = mgr_.CreateGroup("alice", "G", "", "", 0, {});

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; ++i) {
        std::string uid = "u" + std::to_string(t) + "_" + std::to_string(i);
        mgr_.AddMember(gid, uid);
        mgr_.IsMember(gid, uid);
        mgr_.GetMembers(gid);
        mgr_.GetUserGroups(uid);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  EXPECT_EQ(mgr_.GetMembers(gid).size(), 101u);  // owner + 100 members
}

}  // namespace

using chirp::chat::TypingConfig;
using chirp::chat::TypingIndicator;
using chirp::chat::TypingManager;

namespace {

class TypingManagerTest : public ::testing::Test {
protected:
  explicit TypingManagerTest(const TypingConfig& config = TypingConfig())
      : mgr_(config) {}

  TypingManager mgr_;
};

TEST_F(TypingManagerTest, FirstTypingEventNotifies) {
  TypingIndicator notify;
  EXPECT_TRUE(
      mgr_.UserStartedTyping("chan1", chirp::chat::PRIVATE, "alice", "Alice", &notify));

  EXPECT_EQ(notify.channel_id(), "chan1");
  EXPECT_EQ(notify.channel_type(), chirp::chat::PRIVATE);
  EXPECT_EQ(notify.user_id(), "alice");
  EXPECT_EQ(notify.username(), "Alice");
  EXPECT_TRUE(notify.is_typing());
  EXPECT_GT(notify.timestamp(), 0);
}

TEST_F(TypingManagerTest, FirstTypingEventWithoutOutputPointer) {
  EXPECT_TRUE(mgr_.UserStartedTyping("chan1", chirp::chat::PRIVATE, "alice", "Alice"));
}

TEST_F(TypingManagerTest, SubsequentEventsWithinCooldownDoNotNotify) {
  TypingConfig config;
  config.cooldown_ms = 60000;  // Long cooldown
  TypingManager mgr(config);

  EXPECT_TRUE(mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A"));
  // First repeat is still broadcast (initial last_broadcast_time is set in the
  // past to force it), the second repeat hits the cooldown and is suppressed.
  EXPECT_TRUE(mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A"));
  TypingIndicator notify;
  EXPECT_FALSE(mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A", &notify));

  // Different user is unaffected by alice's cooldown
  EXPECT_TRUE(mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "bob", "B"));
}

TEST_F(TypingManagerTest, ZeroCooldownAlwaysNotifies) {
  TypingConfig config;
  config.cooldown_ms = 0;
  TypingManager mgr(config);

  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A"));
  }
}

TEST_F(TypingManagerTest, ChannelsAreIsolatedByTypeAndId) {
  EXPECT_TRUE(mgr_.UserStartedTyping("c1", chirp::chat::PRIVATE, "alice", "A"));
  // Different channel id: separate channel entry
  EXPECT_TRUE(mgr_.UserStartedTyping("c2", chirp::chat::PRIVATE, "alice", "A"));
  // Same id but different type: separate channel entry
  EXPECT_TRUE(mgr_.UserStartedTyping("c1", chirp::chat::TEAM, "alice", "A"));

  EXPECT_EQ(mgr_.GetActiveChannelCount(), 3u);
}

TEST_F(TypingManagerTest, StopTypingRemovesUser) {
  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  EXPECT_EQ(mgr_.GetTotalTypingUserCount(), 1u);

  EXPECT_TRUE(mgr_.UserStoppedTyping("c", chirp::chat::PRIVATE, "alice"));
  EXPECT_EQ(mgr_.GetTotalTypingUserCount(), 0u);
  EXPECT_TRUE(mgr_.GetTypingUsers("c", chirp::chat::PRIVATE).empty());
}

TEST_F(TypingManagerTest, StopTypingCleansUpEmptyChannel) {
  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  EXPECT_EQ(mgr_.GetActiveChannelCount(), 1u);

  EXPECT_TRUE(mgr_.UserStoppedTyping("c", chirp::chat::PRIVATE, "alice"));
  EXPECT_EQ(mgr_.GetActiveChannelCount(), 0u);
}

TEST_F(TypingManagerTest, StopTypingKeepsChannelWhenOthersTyping) {
  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "bob", "B");

  EXPECT_TRUE(mgr_.UserStoppedTyping("c", chirp::chat::PRIVATE, "alice"));
  EXPECT_EQ(mgr_.GetActiveChannelCount(), 1u);
  EXPECT_EQ(mgr_.GetTotalTypingUserCount(), 1u);
}

TEST_F(TypingManagerTest, StopTypingUnknownChannelOrUserReturnsFalse) {
  EXPECT_FALSE(mgr_.UserStoppedTyping("c", chirp::chat::PRIVATE, "alice"));

  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  EXPECT_FALSE(mgr_.UserStoppedTyping("c", chirp::chat::PRIVATE, "bob"));
  // Wrong channel type does not match either
  EXPECT_FALSE(mgr_.UserStoppedTyping("c", chirp::chat::TEAM, "alice"));
}

TEST_F(TypingManagerTest, GetTypingUsersListsActiveUsers) {
  mgr_.UserStartedTyping("c", chirp::chat::WORLD, "alice", "A");
  mgr_.UserStartedTyping("c", chirp::chat::WORLD, "bob", "B");

  auto users = mgr_.GetTypingUsers("c", chirp::chat::WORLD);
  ASSERT_EQ(users.size(), 2u);
  EXPECT_NE(std::find(users.begin(), users.end(), "alice"), users.end());
  EXPECT_NE(std::find(users.begin(), users.end(), "bob"), users.end());
}

TEST_F(TypingManagerTest, GetTypingUsersUnknownChannelEmpty) {
  EXPECT_TRUE(mgr_.GetTypingUsers("c", chirp::chat::PRIVATE).empty());
}

TEST_F(TypingManagerTest, GetTypingUsersRespectsMaxLimit) {
  TypingConfig config;
  config.max_typing_users = 3;
  TypingManager mgr(config);

  for (int i = 0; i < 10; ++i) {
    mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "u" + std::to_string(i), "U");
  }
  EXPECT_EQ(mgr.GetTypingUsers("c", chirp::chat::PRIVATE).size(), 3u);
}

TEST_F(TypingManagerTest, GetTypingIndicatorReturnsStoredState) {
  mgr_.UserStartedTyping("c", chirp::chat::GUILD, "alice", "Alice");

  TypingIndicator ind;
  ASSERT_TRUE(mgr_.GetTypingIndicator("c", chirp::chat::GUILD, "alice", &ind));
  EXPECT_EQ(ind.user_id(), "alice");
  EXPECT_EQ(ind.username(), "Alice");
  EXPECT_EQ(ind.channel_id(), "c");
  EXPECT_EQ(ind.channel_type(), chirp::chat::GUILD);
  EXPECT_TRUE(ind.is_typing());
  EXPECT_GT(ind.timestamp(), 0);
}

TEST_F(TypingManagerTest, GetTypingIndicatorNullPointerReturnsFalse) {
  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  EXPECT_FALSE(mgr_.GetTypingIndicator("c", chirp::chat::PRIVATE, "alice", nullptr));
}

TEST_F(TypingManagerTest, GetTypingIndicatorUnknownsReturnFalse) {
  TypingIndicator ind;
  EXPECT_FALSE(mgr_.GetTypingIndicator("c", chirp::chat::PRIVATE, "alice", &ind));

  mgr_.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  EXPECT_FALSE(mgr_.GetTypingIndicator("c", chirp::chat::PRIVATE, "bob", &ind));
  EXPECT_FALSE(mgr_.GetTypingIndicator("c", chirp::chat::TEAM, "alice", &ind));
}

TEST_F(TypingManagerTest, ExpiredUsersFilteredFromTypingList) {
  TypingConfig config;
  config.typing_timeout_ms = 1;  // 1ms timeout
  TypingManager mgr(config);

  mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "bob", "B");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_TRUE(mgr.GetTypingUsers("c", chirp::chat::PRIVATE).empty());
}

TEST_F(TypingManagerTest, CleanupExpiredRemovesStaleUsersAndEmptyChannels) {
  TypingConfig config;
  config.typing_timeout_ms = 1;
  TypingManager mgr(config);

  mgr.UserStartedTyping("c1", chirp::chat::PRIVATE, "alice", "A");
  mgr.UserStartedTyping("c2", chirp::chat::PRIVATE, "bob", "B");

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  mgr.CleanupExpired();

  EXPECT_EQ(mgr.GetActiveChannelCount(), 0u);
  EXPECT_EQ(mgr.GetTotalTypingUserCount(), 0u);
}

TEST_F(TypingManagerTest, CleanupExpiredKeepsActiveUsers) {
  TypingConfig config;
  config.typing_timeout_ms = 60000;
  TypingManager mgr(config);

  mgr.UserStartedTyping("c", chirp::chat::PRIVATE, "alice", "A");
  mgr.CleanupExpired();

  EXPECT_EQ(mgr.GetActiveChannelCount(), 1u);
  EXPECT_EQ(mgr.GetTotalTypingUserCount(), 1u);
}

TEST_F(TypingManagerTest, StatsCountersTrackState) {
  mgr_.UserStartedTyping("c1", chirp::chat::PRIVATE, "alice", "A");
  mgr_.UserStartedTyping("c1", chirp::chat::PRIVATE, "bob", "B");
  mgr_.UserStartedTyping("c2", chirp::chat::TEAM, "carol", "C");

  EXPECT_EQ(mgr_.GetActiveChannelCount(), 2u);
  EXPECT_EQ(mgr_.GetTotalTypingUserCount(), 3u);
}

TEST_F(TypingManagerTest, ConcurrentTypingUpdatesAreSafe) {
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; ++i) {
        std::string uid = "u" + std::to_string(t) + "_" + std::to_string(i);
        mgr_.UserStartedTyping("c", chirp::chat::WORLD, uid, uid);
        mgr_.GetTypingUsers("c", chirp::chat::WORLD);
        if (i % 5 == 0) {
          mgr_.UserStoppedTyping("c", chirp::chat::WORLD, uid);
        }
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  EXPECT_LE(mgr_.GetTotalTypingUserCount(), 100u);
  EXPECT_GE(mgr_.GetActiveChannelCount(), 1u);
}

}  // namespace

using chirp::chat::MessageReaction;
using chirp::chat::ReactionManager;

namespace {

class ReactionManagerTest : public ::testing::Test {
protected:
  ReactionManager mgr_;
};

TEST_F(ReactionManagerTest, AddReactionReturnsPopulatedOutput) {
  MessageReaction out;
  EXPECT_TRUE(mgr_.AddReaction("m1", "alice", "👍", &out));

  EXPECT_EQ(out.message_id(), "m1");
  EXPECT_EQ(out.emoji(), "👍");
  EXPECT_EQ(out.count(), 1);
  ASSERT_EQ(out.user_ids_size(), 1);
  EXPECT_EQ(out.user_ids(0), "alice");
}

TEST_F(ReactionManagerTest, AddReactionWithoutOutputPointer) {
  EXPECT_TRUE(mgr_.AddReaction("m1", "alice", "👍", nullptr));
}

TEST_F(ReactionManagerTest, AddReactionMultipleUsersAggregatesCount) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  MessageReaction out;
  EXPECT_TRUE(mgr_.AddReaction("m1", "bob", "👍", &out));
  EXPECT_EQ(out.count(), 2);
  EXPECT_EQ(out.user_ids_size(), 2);
}

TEST_F(ReactionManagerTest, AddReactionSameUserIsIdempotent) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  MessageReaction out;
  EXPECT_TRUE(mgr_.AddReaction("m1", "alice", "👍", &out));
  EXPECT_EQ(out.count(), 1);
  EXPECT_EQ(out.user_ids_size(), 1);
}

TEST_F(ReactionManagerTest, AddReactionMultipleEmojis) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  MessageReaction out;
  EXPECT_TRUE(mgr_.AddReaction("m1", "bob", "❤️", &out));

  EXPECT_EQ(mgr_.GetReactions("m1").size(), 2u);
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 2u);
}

TEST_F(ReactionManagerTest, RemoveReactionDeletesMembership) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  EXPECT_TRUE(mgr_.HasReacted("m1", "alice", "👍"));

  EXPECT_TRUE(mgr_.RemoveReaction("m1", "alice", "👍"));
  EXPECT_FALSE(mgr_.HasReacted("m1", "alice", "👍"));
}

TEST_F(ReactionManagerTest, RemoveReactionCleansUpEmptyEntries) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  ASSERT_TRUE(mgr_.RemoveReaction("m1", "alice", "👍"));

  // Both the emoji entry and the message entry should be cleaned up
  EXPECT_TRUE(mgr_.GetReactions("m1").empty());
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 0u);
  EXPECT_TRUE(mgr_.GetUserReactions("m1", "alice").empty());
}

TEST_F(ReactionManagerTest, RemoveReactionKeepsOtherUsersAndEmojis) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  mgr_.AddReaction("m1", "bob", "👍", nullptr);
  mgr_.AddReaction("m1", "alice", "❤️", nullptr);

  EXPECT_TRUE(mgr_.RemoveReaction("m1", "alice", "👍"));

  EXPECT_FALSE(mgr_.HasReacted("m1", "alice", "👍"));
  EXPECT_TRUE(mgr_.HasReacted("m1", "bob", "👍"));
  EXPECT_TRUE(mgr_.HasReacted("m1", "alice", "❤️"));
}

TEST_F(ReactionManagerTest, RemoveReactionMissingEntriesReturnsFalse) {
  EXPECT_FALSE(mgr_.RemoveReaction("m1", "alice", "👍"));  // no message entry

  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  EXPECT_FALSE(mgr_.RemoveReaction("m1", "alice", "❤️"));  // no emoji entry
}

TEST_F(ReactionManagerTest, GetReactionsSortedByCountDescending) {
  mgr_.AddReaction("m1", "u1", "👍", nullptr);
  mgr_.AddReaction("m1", "u2", "👍", nullptr);
  mgr_.AddReaction("m1", "u3", "👍", nullptr);
  mgr_.AddReaction("m1", "u1", "❤️", nullptr);

  auto reactions = mgr_.GetReactions("m1");
  ASSERT_EQ(reactions.size(), 2u);
  EXPECT_EQ(reactions[0].emoji(), "👍");
  EXPECT_EQ(reactions[0].count(), 3);
  EXPECT_EQ(reactions[1].emoji(), "❤️");
  EXPECT_EQ(reactions[1].count(), 1);
}

TEST_F(ReactionManagerTest, GetReactionsUnknownMessageEmpty) {
  EXPECT_TRUE(mgr_.GetReactions("missing").empty());
}

TEST_F(ReactionManagerTest, GetReactionSpecificEmoji) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  mgr_.AddReaction("m1", "bob", "👍", nullptr);

  MessageReaction out;
  ASSERT_TRUE(mgr_.GetReaction("m1", "👍", &out));
  EXPECT_EQ(out.emoji(), "👍");
  EXPECT_EQ(out.count(), 2);
  EXPECT_EQ(out.user_ids_size(), 2);
}

TEST_F(ReactionManagerTest, GetReactionNullPointerOrMissingReturnsFalse) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);

  MessageReaction out;
  EXPECT_FALSE(mgr_.GetReaction("m1", "👍", nullptr));
  EXPECT_FALSE(mgr_.GetReaction("missing", "👍", &out));
  EXPECT_FALSE(mgr_.GetReaction("m1", "❤️", &out));
}

TEST_F(ReactionManagerTest, HasReactedVariousCases) {
  EXPECT_FALSE(mgr_.HasReacted("m1", "alice", "👍"));

  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  EXPECT_TRUE(mgr_.HasReacted("m1", "alice", "👍"));
  EXPECT_FALSE(mgr_.HasReacted("m1", "bob", "👍"));
  EXPECT_FALSE(mgr_.HasReacted("m1", "alice", "❤️"));
  EXPECT_FALSE(mgr_.HasReacted("m2", "alice", "👍"));
}

TEST_F(ReactionManagerTest, GetUserReactionsListsAllEmojis) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  mgr_.AddReaction("m1", "alice", "❤️", nullptr);
  mgr_.AddReaction("m1", "bob", "🎉", nullptr);

  auto alice = mgr_.GetUserReactions("m1", "alice");
  ASSERT_EQ(alice.size(), 2u);
  EXPECT_NE(std::find(alice.begin(), alice.end(), "👍"), alice.end());
  EXPECT_NE(std::find(alice.begin(), alice.end(), "❤️"), alice.end());

  auto bob = mgr_.GetUserReactions("m1", "bob");
  ASSERT_EQ(bob.size(), 1u);
  EXPECT_EQ(bob[0], "🎉");

  EXPECT_TRUE(mgr_.GetUserReactions("m1", "carol").empty());
  EXPECT_TRUE(mgr_.GetUserReactions("missing", "alice").empty());
}

TEST_F(ReactionManagerTest, ClearMessageReactionsRemovesEverything) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  mgr_.AddReaction("m1", "bob", "❤️", nullptr);
  mgr_.AddReaction("m2", "alice", "👍", nullptr);

  mgr_.ClearMessageReactions("m1");

  EXPECT_TRUE(mgr_.GetReactions("m1").empty());
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 0u);
  // Other messages unaffected
  EXPECT_EQ(mgr_.GetTotalReactionCount("m2"), 1u);
}

TEST_F(ReactionManagerTest, ClearMessageReactionsOnUnknownIsNoop) {
  mgr_.ClearMessageReactions("missing");  // Must not crash
  EXPECT_TRUE(mgr_.GetReactions("missing").empty());
}

TEST_F(ReactionManagerTest, GetTotalReactionCountSumsAcrossEmojis) {
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 0u);

  mgr_.AddReaction("m1", "u1", "👍", nullptr);
  mgr_.AddReaction("m1", "u2", "👍", nullptr);
  mgr_.AddReaction("m1", "u1", "❤️", nullptr);
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 3u);
}

TEST_F(ReactionManagerTest, GetTopReactionsAppliesLimit) {
  mgr_.AddReaction("m1", "u1", "👍", nullptr);
  mgr_.AddReaction("m1", "u2", "👍", nullptr);
  mgr_.AddReaction("m1", "u3", "🎉", nullptr);
  mgr_.AddReaction("m1", "u4", "❤️", nullptr);

  auto top2 = mgr_.GetTopReactions("m1", 2);
  ASSERT_EQ(top2.size(), 2u);
  EXPECT_EQ(top2[0].emoji(), "👍");  // Most popular first
  EXPECT_EQ(top2[0].count(), 2);

  auto all = mgr_.GetTopReactions("m1", 10);
  EXPECT_EQ(all.size(), 3u);  // Limit larger than result set
}

TEST_F(ReactionManagerTest, GetReactionsForMessagesBulk) {
  mgr_.AddReaction("m1", "alice", "👍", nullptr);
  mgr_.AddReaction("m2", "bob", "❤️", nullptr);
  mgr_.AddReaction("m2", "carol", "❤️", nullptr);

  auto result = mgr_.GetReactionsForMessages({"m1", "m2", "m3"});

  ASSERT_EQ(result.size(), 3u);

  ASSERT_EQ(result["m1"].size(), 1u);
  EXPECT_EQ(result["m1"][0].emoji(), "👍");
  EXPECT_EQ(result["m1"][0].count(), 1);

  ASSERT_EQ(result["m2"].size(), 1u);
  EXPECT_EQ(result["m2"][0].emoji(), "❤️");
  EXPECT_EQ(result["m2"][0].count(), 2);

  EXPECT_TRUE(result["m3"].empty());  // No reactions -> empty vector
}

TEST_F(ReactionManagerTest, ManyUsersOmitUserIdsList) {
  for (int i = 0; i < 12; ++i) {
    mgr_.AddReaction("m1", "u" + std::to_string(i), "👍", nullptr);
  }

  MessageReaction out;
  ASSERT_TRUE(mgr_.GetReaction("m1", "👍", &out));
  EXPECT_EQ(out.count(), 12);
  EXPECT_EQ(out.user_ids_size(), 0);  // > 10 users: ids omitted
}

TEST_F(ReactionManagerTest, ConcurrentAddsAreSafe) {
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; ++i) {
        std::string uid = "u" + std::to_string(t) + "_" + std::to_string(i);
        mgr_.AddReaction("m1", uid, "👍", nullptr);
        mgr_.GetReactions("m1");
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  EXPECT_EQ(mgr_.GetTotalReactionCount("m1"), 100u);
}

}  // namespace

using chirp::chat::ReadReceipt;
using chirp::chat::ReadReceiptManager;

namespace {

class ReadReceiptManagerTest : public ::testing::Test {
protected:
  ReadReceiptManager mgr_;
};

TEST_F(ReadReceiptManagerTest, MarkReadStoresReceipt) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);

  auto receipts = mgr_.GetReadReceipts("m1");
  ASSERT_EQ(receipts.size(), 1u);
  EXPECT_EQ(receipts[0].user_id(), "alice");
  EXPECT_EQ(receipts[0].message_id(), "m1");
  EXPECT_EQ(receipts[0].read_at(), 1000);
}

TEST_F(ReadReceiptManagerTest, MultipleReadersProduceMultipleReceipts) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);
  mgr_.MarkRead("bob", "chan", chirp::chat::PRIVATE, "m1", 2000);

  auto receipts = mgr_.GetReadReceipts("m1");
  ASSERT_EQ(receipts.size(), 2u);
  EXPECT_NE(std::find_if(receipts.begin(), receipts.end(),
                         [](const ReadReceipt& r) { return r.user_id() == "alice"; }),
            receipts.end());
  EXPECT_NE(std::find_if(receipts.begin(), receipts.end(),
                         [](const ReadReceipt& r) { return r.user_id() == "bob"; }),
            receipts.end());
}

TEST_F(ReadReceiptManagerTest, SameUserRereadingAppendsReceipt) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 3000);

  auto receipts = mgr_.GetReadReceipts("m1");
  EXPECT_EQ(receipts.size(), 2u);
}

TEST_F(ReadReceiptManagerTest, ReceiptsAreScopedPerMessage) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m2", 2000);

  EXPECT_EQ(mgr_.GetReadReceipts("m1").size(), 1u);
  EXPECT_EQ(mgr_.GetReadReceipts("m2").size(), 1u);
  EXPECT_TRUE(mgr_.GetReadReceipts("m3").empty());
}

TEST_F(ReadReceiptManagerTest, GetReadCursorReturnsLatestState) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);

  std::string last_id;
  int64_t ts = 0;
  ASSERT_TRUE(mgr_.GetReadCursor("alice", "chan", chirp::chat::PRIVATE, &last_id, &ts));
  EXPECT_EQ(last_id, "m1");
  EXPECT_EQ(ts, 1000);

  // Advance the cursor
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m2", 2000);
  ASSERT_TRUE(mgr_.GetReadCursor("alice", "chan", chirp::chat::PRIVATE, &last_id, &ts));
  EXPECT_EQ(last_id, "m2");
  EXPECT_EQ(ts, 2000);
}

TEST_F(ReadReceiptManagerTest, GetReadCursorIsNullTolerant) {
  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);
  EXPECT_TRUE(mgr_.GetReadCursor("alice", "chan", chirp::chat::PRIVATE, nullptr, nullptr));
}

TEST_F(ReadReceiptManagerTest, GetReadCursorMissingReturnsFalse) {
  std::string id;
  int64_t ts = 0;
  // Unknown user
  EXPECT_FALSE(mgr_.GetReadCursor("nobody", "chan", chirp::chat::PRIVATE, &id, &ts));

  mgr_.MarkRead("alice", "chan", chirp::chat::PRIVATE, "m1", 1000);
  // Known user, unknown channel
  EXPECT_FALSE(mgr_.GetReadCursor("alice", "other", chirp::chat::PRIVATE, &id, &ts));
  // Known user + channel, different type
  EXPECT_FALSE(mgr_.GetReadCursor("alice", "chan", chirp::chat::TEAM, &id, &ts));
}

TEST_F(ReadReceiptManagerTest, CursorsAreIsolatedPerUserAndChannel) {
  mgr_.MarkRead("alice", "c1", chirp::chat::PRIVATE, "m1", 1000);
  mgr_.MarkRead("alice", "c2", chirp::chat::PRIVATE, "m2", 2000);
  mgr_.MarkRead("bob", "c1", chirp::chat::PRIVATE, "m3", 3000);

  std::string id;
  int64_t ts = 0;

  ASSERT_TRUE(mgr_.GetReadCursor("alice", "c1", chirp::chat::PRIVATE, &id, &ts));
  EXPECT_EQ(id, "m1");
  ASSERT_TRUE(mgr_.GetReadCursor("alice", "c2", chirp::chat::PRIVATE, &id, &ts));
  EXPECT_EQ(id, "m2");
  ASSERT_TRUE(mgr_.GetReadCursor("bob", "c1", chirp::chat::PRIVATE, &id, &ts));
  EXPECT_EQ(id, "m3");
}

TEST_F(ReadReceiptManagerTest, GetUnreadCountForUserWithoutCursors) {
  EXPECT_EQ(mgr_.GetUnreadCount("nobody"), 0);
}

TEST_F(ReadReceiptManagerTest, GetAllUnreadListsTrackedChannels) {
  mgr_.MarkRead("alice", "c1", chirp::chat::TEAM, "m1", 1000);
  mgr_.MarkRead("alice", "c2", chirp::chat::WORLD, "m2", 2000);

  auto unread = mgr_.GetAllUnread("alice");
  ASSERT_EQ(unread.size(), 2u);

  bool saw_c1 = false, saw_c2 = false;
  for (const auto& cu : unread) {
    EXPECT_EQ(cu.count(), 0);  // unread counts are not modified by MarkRead
    if (cu.channel_id() == "c1") {
      saw_c1 = true;
      EXPECT_EQ(cu.channel_type(), chirp::chat::TEAM);
      EXPECT_EQ(cu.last_message_id(), "m1");
    } else if (cu.channel_id() == "c2") {
      saw_c2 = true;
      EXPECT_EQ(cu.channel_type(), chirp::chat::WORLD);
      EXPECT_EQ(cu.last_message_id(), "m2");
    }
  }
  EXPECT_TRUE(saw_c1);
  EXPECT_TRUE(saw_c2);
}

TEST_F(ReadReceiptManagerTest, GetAllUnreadUnknownUserEmpty) {
  EXPECT_TRUE(mgr_.GetAllUnread("nobody").empty());
}

TEST_F(ReadReceiptManagerTest, TrackMessageIsSafeToCall) {
  mgr_.TrackMessage("m1", "chan", chirp::chat::GUILD);
  mgr_.TrackMessage("m1", "other", chirp::chat::TEAM);  // duplicate is ignored
  mgr_.TrackMessage("m2", "chan", chirp::chat::GUILD);
  // Must not crash; message tracking has no observable getter.
  SUCCEED();
}

TEST_F(ReadReceiptManagerTest, ConcurrentMarkReadsAreSafe) {
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; ++i) {
        std::string uid = "u" + std::to_string(t);
        std::string mid = "m" + std::to_string(i);
        mgr_.MarkRead(uid, "chan", chirp::chat::PRIVATE, mid, 1000 + i);
        mgr_.GetReadReceipts(mid);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  // Each message got one receipt per user thread = 4 receipts
  EXPECT_EQ(mgr_.GetReadReceipts("m0").size(), 4u);
  EXPECT_EQ(mgr_.GetReadReceipts("m24").size(), 4u);
}

}  // namespace

// ---------------------------------------------------------------------------
// Distributed dispatch / runtime utils tests (mock Session, no network IO)
// ---------------------------------------------------------------------------

#include <cstring>

#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "network/protobuf_framing.h"
#include "runtime_utils.h"

using chirp::network::ProtobufFraming;
using chirp::network::Session;
using chirp::chat::runtime::DistributedDispatchHandlers;
using chirp::chat::runtime::DispatchDistributedPacket;
using chirp::chat::runtime::MakeDistributedTcpServer;
using chirp::chat::runtime::MakeDistributedWsServer;
using chirp::chat::runtime::InstallSignalStop;
using chirp::gateway::HeartbeatPong;
using chirp::gateway::Packet;
namespace {

// Pure in-memory Session mock: records everything sent through it.
class MockSession : public Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

Packet MakePacket(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
template <typename T>
bool DecodeFramed(const std::string& framed, T* out) {
  if (framed.size() < 4u) {
    return false;
  }
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  if (framed.size() != 4u + static_cast<size_t>(len)) {
    return false;
  }
  return out->ParseFromString(framed.substr(4, len));
}

class DispatchTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();
};

TEST_F(DispatchTest, LoginRequestDispatched) {
  chirp::auth::LoginRequest req;
  req.set_token("tok");
  req.set_device_id("dev");

  DistributedDispatchHandlers handlers;
  chirp::auth::LoginRequest got;
  int64_t got_seq = -1;
  handlers.on_login = [&](const std::shared_ptr<Session>& s,
                          const chirp::auth::LoginRequest& r, int64_t seq) {
    ASSERT_EQ(s, session_);
    got = r;
    got_seq = seq;
  };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::LOGIN_REQ, 42,
                                                 req.SerializeAsString()),
                            handlers);
  EXPECT_EQ(got.token(), "tok");
  EXPECT_EQ(got.device_id(), "dev");
  EXPECT_EQ(got_seq, 42);
}

TEST_F(DispatchTest, SendMessageRequestDispatched) {
  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_content("hi");

  DistributedDispatchHandlers handlers;
  chirp::chat::SendMessageRequest got;
  handlers.on_send_message = [&](const std::shared_ptr<Session>&,
                                 const chirp::chat::SendMessageRequest& r, int64_t seq) {
    got = r;
    EXPECT_EQ(seq, 7);
  };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::SEND_MESSAGE_REQ, 7,
                                                 req.SerializeAsString()),
                            handlers);
  EXPECT_EQ(got.sender_id(), "alice");
  EXPECT_EQ(got.content(), "hi");
}

TEST_F(DispatchTest, GetHistoryRequestDispatched) {
  chirp::chat::GetHistoryRequest req;
  req.set_channel_id("c1");

  DistributedDispatchHandlers handlers;
  chirp::chat::GetHistoryRequest got;
  handlers.on_get_history = [&](const std::shared_ptr<Session>&,
                                const chirp::chat::GetHistoryRequest& r, int64_t seq) {
    got = r;
    EXPECT_EQ(seq, 9);
  };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::GET_HISTORY_REQ, 9,
                                                 req.SerializeAsString()),
                            handlers);
  EXPECT_EQ(got.channel_id(), "c1");
}

TEST_F(DispatchTest, GetHistoryV2ReceivesRawBody) {
  DistributedDispatchHandlers handlers;
  std::string got_body;
  int64_t got_seq = -1;
  handlers.on_get_history_v2 = [&](const std::shared_ptr<Session>& s,
                                   const std::string& body, int64_t seq) {
    ASSERT_EQ(s, session_);
    got_body = body;
    got_seq = seq;
  };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::GET_HISTORY_V2_REQ, 11, "RAWBODY"),
                            handlers);
  EXPECT_EQ(got_body, "RAWBODY");
  EXPECT_EQ(got_seq, 11);
}

TEST_F(DispatchTest, LogoutRequestDispatched) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");

  DistributedDispatchHandlers handlers;
  chirp::auth::LogoutRequest got;
  handlers.on_logout = [&](const std::shared_ptr<Session>&,
                           const chirp::auth::LogoutRequest& r, int64_t seq) {
    got = r;
    EXPECT_EQ(seq, 3);
  };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::LOGOUT_REQ, 3,
                                                 req.SerializeAsString()),
                            handlers);
  EXPECT_EQ(got.user_id(), "alice");
}

TEST_F(DispatchTest, HeartbeatPingRepliesWithPong) {
  DistributedDispatchHandlers handlers;  // no handlers needed
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::HEARTBEAT_PING, 77, ""),
                            handlers);

  ASSERT_EQ(session_->sent.size(), 1u);
  Packet pong;
  ASSERT_TRUE(DecodeFramed(session_->sent[0], &pong));
  EXPECT_EQ(pong.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  EXPECT_EQ(pong.sequence(), 77);

  HeartbeatPong hp;
  ASSERT_TRUE(hp.ParseFromString(pong.body()));
  EXPECT_GT(hp.timestamp(), 0);
  EXPECT_GT(hp.server_time(), 0);
}

TEST_F(DispatchTest, UnknownMsgIdIsIgnored) {
  DistributedDispatchHandlers handlers;
  bool any_called = false;
  handlers.on_login = [&](auto&&, auto&&, auto&&) { any_called = true; };

  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::UNKNOWN, 1, "junk"), handlers);
  EXPECT_FALSE(any_called);
  EXPECT_TRUE(session_->sent.empty());
}

TEST_F(DispatchTest, EmptyHandlersAreSafe) {
  DistributedDispatchHandlers handlers;  // all empty
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::LOGIN_REQ, 1, ""), handlers);
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::SEND_MESSAGE_REQ, 2, ""), handlers);
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::GET_HISTORY_REQ, 3, ""), handlers);
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::GET_HISTORY_V2_REQ, 4, ""), handlers);
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::LOGOUT_REQ, 5, ""), handlers);
  // No handlers, but HEARTBEAT still replies
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::HEARTBEAT_PING, 6, ""), handlers);
  EXPECT_EQ(session_->sent.size(), 1u);
}

TEST_F(DispatchTest, MalformedBodySkipsHandler) {
  DistributedDispatchHandlers handlers;
  bool called = false;
  handlers.on_login = [&](auto&&, auto&&, auto&&) { called = true; };

  // Invalid protobuf body -> handler must not fire
  DispatchDistributedPacket(session_,
                            MakePacket(chirp::gateway::LOGIN_REQ, 1, std::string("\xff\xff\xff\xff", 4)),
                            handlers);
  EXPECT_FALSE(called);

  // Empty body parses as default LoginRequest -> handler fires
  DispatchDistributedPacket(session_, MakePacket(chirp::gateway::LOGIN_REQ, 2, ""), handlers);
  EXPECT_TRUE(called);
}

class RuntimeUtilsTest : public ::testing::Test {};

TEST_F(RuntimeUtilsTest, NowMsAdvances) {
  using chirp::chat::runtime::NowMs;
  int64_t t1 = NowMs();
  EXPECT_GT(t1, 0);
  EXPECT_GE(NowMs(), t1);
}

TEST_F(RuntimeUtilsTest, GetArgFindsValues) {
  using chirp::chat::runtime::GetArg;
  char arg0[] = "prog";
  char arg1[] = "--port";
  char arg2[] = "8080";
  char arg3[] = "--flag";
  char* argv[] = {arg0, arg1, arg2, arg3, nullptr};

  EXPECT_EQ(GetArg(4, argv, "--port", "x"), "8080");
  EXPECT_EQ(GetArg(4, argv, "--missing", "def"), "def");
  // Key present but no value after it -> default
  EXPECT_EQ(GetArg(4, argv, "--flag", "def"), "def");
  EXPECT_EQ(GetArg(0, argv, "--port", "def"), "def");
}

TEST_F(RuntimeUtilsTest, ParseU16Arg) {
  using chirp::chat::runtime::ParseU16Arg;
  char arg0[] = "prog";
  char arg1[] = "--port";
  char arg2[] = "9090";
  char arg3[] = "--bad";
  char arg4[] = "notanumber";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4, nullptr};

  EXPECT_EQ(ParseU16Arg(5, argv, "--port", 1000), 9090);
  EXPECT_EQ(ParseU16Arg(5, argv, "--absent", 1234), 1234);
  EXPECT_EQ(ParseU16Arg(5, argv, "--bad", 77), 0);  // atoi("notanumber") == 0
}

TEST_F(RuntimeUtilsTest, ParseIntArg) {
  using chirp::chat::runtime::ParseIntArg;
  char arg0[] = "prog";
  char arg1[] = "--n";
  char arg2[] = "-5";
  char* argv[] = {arg0, arg1, arg2, nullptr};

  EXPECT_EQ(ParseIntArg(3, argv, "--n", 1), -5);
  EXPECT_EQ(ParseIntArg(3, argv, "--absent", 99), 99);
}

TEST_F(RuntimeUtilsTest, RandomHexFormatAndUniqueness) {
  using chirp::chat::runtime::RandomHex;
  EXPECT_EQ(RandomHex(0).size(), 0u);

  std::string hex = RandomHex(8);
  EXPECT_EQ(hex.size(), 16u);
  for (char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
  EXPECT_NE(RandomHex(8), RandomHex(8));
}

TEST_F(RuntimeUtilsTest, GenerateMessageIdFormatAndUnique) {
  using chirp::chat::runtime::GenerateMessageId;
  std::string id = GenerateMessageId();
  EXPECT_EQ(id.substr(0, 4), "msg_");
  EXPECT_NE(id, GenerateMessageId());
}

TEST_F(RuntimeUtilsTest, SendPacketFramesCorrectly) {
  using chirp::chat::runtime::SendPacket;
  auto session = std::make_shared<MockSession>();

  SendPacket(session, chirp::gateway::KICK_NOTIFY, 123, "payload");
  ASSERT_EQ(session->sent.size(), 1u);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(pkt.sequence(), 123);
  EXPECT_EQ(pkt.body(), "payload");
}

TEST_F(RuntimeUtilsTest, SendChatNotifyFramesCorrectly) {
  using chirp::chat::runtime::SendChatNotify;
  auto session = std::make_shared<MockSession>();

  chirp::chat::ChatMessage msg;
  msg.set_message_id("m1");
  msg.set_sender_id("alice");
  msg.set_content("hello");

  SendChatNotify(session, msg);
  ASSERT_EQ(session->sent.size(), 1u);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::CHAT_MESSAGE_NOTIFY);
  EXPECT_EQ(pkt.sequence(), 0);

  chirp::chat::ChatMessage decoded;
  ASSERT_TRUE(decoded.ParseFromString(pkt.body()));
  EXPECT_EQ(decoded.message_id(), "m1");
  EXPECT_EQ(decoded.sender_id(), "alice");
  EXPECT_EQ(decoded.content(), "hello");
}

class DistributedRuntimeTest : public ::testing::Test {};

TEST_F(DistributedRuntimeTest, MakeTcpServerAndStop) {
  asio::io_context io;
  bool called = false;
  auto server = MakeDistributedTcpServer(
      io, /*port=*/0,  // ephemeral port, bind only
      [&](const std::shared_ptr<Session>&, const chirp::gateway::Packet&) { called = true; },
      [](const std::shared_ptr<Session>&) {});
  ASSERT_NE(server, nullptr);
  server->Stop();  // must not block or throw
  EXPECT_FALSE(called);
}

TEST_F(DistributedRuntimeTest, MakeWsServerAndStop) {
  asio::io_context io;
  auto server = MakeDistributedWsServer(
      io, /*port=*/0,
      [](const std::shared_ptr<Session>&, const chirp::gateway::Packet&) {},
      [](const std::shared_ptr<Session>&) {});
  ASSERT_NE(server, nullptr);
  server->Stop();
}

TEST_F(DistributedRuntimeTest, InstallSignalStopInvokesShutdownOnSigterm) {
  asio::io_context io;
  std::promise<void> promise;
  auto future = promise.get_future();

  InstallSignalStop(io, [&promise] { promise.set_value(); });

  // The signal_set constructor registered its handler, so raise() is
  // captured by asio instead of terminating the process.
  raise(SIGTERM);

  io.run_for(std::chrono::milliseconds(2000));
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)),
            std::future_status::ready);
}

namespace {

uint16_t FreeTcpPort() {
  asio::io_context io;
  asio::ip::tcp::acceptor a(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  return static_cast<uint16_t>(a.local_endpoint().port());
}

std::string LpWrapRaw(const std::string& payload) {
  const uint32_t len = static_cast<uint32_t>(payload.size());
  std::string out;
  out.push_back(static_cast<char>((len >> 24) & 0xFF));
  out.push_back(static_cast<char>((len >> 16) & 0xFF));
  out.push_back(static_cast<char>((len >> 8) & 0xFF));
  out.push_back(static_cast<char>(len & 0xFF));
  out += payload;
  return out;
}

bool WaitForCond(const std::function<bool()>& pred, int timeout_ms = 3000) {
  for (int i = 0; i < timeout_ms / 2; ++i) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return pred();
}

}  // namespace

TEST_F(DistributedRuntimeTest, TcpServerDataAndDisconnectPaths) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  const uint16_t port = FreeTcpPort();
  std::atomic<int> packets{0};
  std::atomic<int> disconnects{0};
  auto server = MakeDistributedTcpServer(
      io, port,
      [&](const std::shared_ptr<Session>&, const chirp::gateway::Packet&) { ++packets; },
      [&](const std::shared_ptr<Session>&) { ++disconnects; });
  server->Start();

  chirp::network::TcpClient client(io);
  ASSERT_TRUE(client.Connect("127.0.0.1", port));

  // Garbage payload: DispatchPacket logs the parse failure and drops it.
  client.GetSession()->Send(LpWrapRaw("\xff\xff\xff\xff"));
  // A valid framed packet reaches the handler.
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  pkt.set_sequence(7);
  client.GetSession()->Send(LpWrapRaw(pkt.SerializeAsString()));

  ASSERT_TRUE(WaitForCond([&] { return packets.load() >= 1; }));
  client.Disconnect();
  EXPECT_TRUE(WaitForCond([&] { return disconnects.load() >= 1; }));

  server->Stop();
  io.stop();
  runner.join();
}

TEST_F(DistributedRuntimeTest, WsServerDataAndDisconnectPaths) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  const uint16_t port = FreeTcpPort();
  std::atomic<int> packets{0};
  std::atomic<int> disconnects{0};
  auto server = MakeDistributedWsServer(
      io, port,
      [&](const std::shared_ptr<Session>&, const chirp::gateway::Packet&) { ++packets; },
      [&](const std::shared_ptr<Session>&) { ++disconnects; });
  server->Start();

  chirp::network::WebSocketClient client(io);
  ASSERT_TRUE(client.Connect("127.0.0.1", port, "/ws"));

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  pkt.set_sequence(1);
  client.GetSession()->Send(LpWrapRaw(pkt.SerializeAsString()));

  ASSERT_TRUE(WaitForCond([&] { return packets.load() >= 1; }));
  client.Disconnect();
  EXPECT_TRUE(WaitForCond([&] { return disconnects.load() >= 1; }));

  server->Stop();
  io.stop();
  runner.join();
}

}  // namespace

// ---------------------------------------------------------------------------
// main_distributed.cc internals: the .cc is included with main() renamed so
// the anonymous-namespace handlers can be exercised directly.
// ---------------------------------------------------------------------------

#ifndef CHIRP_SKIP_MAIN_DISTRIBUTED_INCLUDE
#include "fake_servers.h"

#define main chirp_chat_distributed_main
#include "main_distributed.cc"
#undef main
#endif

namespace {

using ChatSessionPtr = std::shared_ptr<chirp::network::Session>;

class DistributedInternalsTest : public ::testing::Test {
 protected:
  std::shared_ptr<DistributedChatState> state_ = std::make_shared<DistributedChatState>();
  std::shared_ptr<DistributedMessageStore> store_ = std::make_shared<DistributedMessageStore>();
  // Scaffold login and a disabled push bridge: the internals tests below
  // call the handlers directly.
  chirp::chat::LoginTokenVerifier verifier_{""};
  chirp::chat::PushBridge push_{nullptr};
};

TEST_F(DistributedInternalsTest, ChatStateSessionLifecycle) {
  auto s1 = std::make_shared<MockSession>();
  auto s2 = std::make_shared<MockSession>();

  state_->AddSession("alice", s1);
  state_->AddSession("bob", s2);

  EXPECT_TRUE(state_->IsUserLocal("alice"));
  EXPECT_EQ(state_->GetUserId(s1.get()), "alice");
  EXPECT_EQ(state_->GetLocalSession("alice"), s1);

  // Session destroyed -> tracked weak_ptr expires
  state_->AddSession("temp", std::make_shared<MockSession>());
  { auto tmp = state_->GetLocalSession("temp"); }
  // Remove by session pointer
  state_->RemoveSession(s1.get());
  EXPECT_FALSE(state_->IsUserLocal("alice"));
  EXPECT_EQ(state_->GetUserId(s1.get()), "");

  // Removing an unknown session is a no-op
  state_->RemoveSession(nullptr);
  EXPECT_TRUE(state_->IsUserLocal("bob"));
}

TEST_F(DistributedInternalsTest, StaleDisconnectKeepsNewerSession) {
  auto old_session = std::make_shared<MockSession>();
  auto new_session = std::make_shared<MockSession>();

  // The first connection registers, a re-login takes over the user slot,
  // and only THEN the old connection's disconnect arrives (e.g. the send
  // client's late FIN). The stale disconnect must not unregister the
  // session that now owns the user.
  state_->AddSession("carol", old_session);
  state_->AddSession("carol", new_session);
  state_->RemoveSession(old_session.get());

  EXPECT_TRUE(state_->IsUserLocal("carol"));
  EXPECT_EQ(state_->GetLocalSession("carol"), new_session);
  EXPECT_EQ(state_->GetUserId(old_session.get()), "");
  EXPECT_EQ(state_->GetUserId(new_session.get()), "carol");
}

TEST_F(DistributedInternalsTest, MessageStoreKeyHelpers) {
  EXPECT_EQ(store_->OfflineKey("u1"), "chirp:chat:offline:u1");
  EXPECT_EQ(store_->HistoryKey("c1"), "chirp:chat:history:c1");
  EXPECT_EQ(store_->PrivateChannelId("a", "b"), "a|b");
  EXPECT_EQ(store_->PrivateChannelId("b", "a"), "a|b");
}

TEST_F(DistributedInternalsTest, MessageStoreWithoutRedisIsNoop) {
  store_->redis = nullptr;  // no Redis configured

  store_->AddOffline("u1", "m");                    // returns early
  EXPECT_TRUE(store_->PopOffline("u1").empty());    // returns {}
  store_->AddToHistory("c1", "m");                  // returns early
  EXPECT_TRUE(store_->GetHistory("c1", 10).empty());
  SUCCEED();
}

TEST_F(DistributedInternalsTest, HandleLoginSuccessRegistersSession) {
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  auto session = std::make_shared<MockSession>();
  state_->instance_id = "inst";

  chirp::auth::LoginRequest req;
  req.set_token("alice");

  HandleLogin(req, session, state_, store_, router, &verifier_, /*seq=*/5);

  ASSERT_EQ(session->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(pkt.sequence(), 5);

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "alice");
  EXPECT_TRUE(state_->IsUserLocal("alice"));
}

TEST_F(DistributedInternalsTest, HandleLoginEmptyTokenRejected) {
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  auto session = std::make_shared<MockSession>();

  chirp::auth::LoginRequest req;  // empty token
  HandleLogin(req, session, state_, store_, router, &verifier_, 1);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(state_->IsUserLocal(""));
}

TEST_F(DistributedInternalsTest, HandleSendMessagePrivateEmptyReceiverRejected) {
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  auto session = std::make_shared<MockSession>();

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_receiver_id("");  // invalid for private messages

  HandleSendMessage(req, session, state_, store_, router, push_, /*seq=*/9);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::SEND_MESSAGE_RESP);
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(DistributedInternalsTest, HandleSendMessageDeliversToOnlineReceiver) {
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);

  auto sender = std::make_shared<MockSession>();
  auto receiver = std::make_shared<MockSession>();
  state_->AddSession("bob", receiver);

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");

  HandleSendMessage(req, sender, state_, store_, router, push_, /*seq=*/3);

  // Sender got an OK response
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(sender->sent[0], &pkt));
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.message_id().empty());

  // Receiver got a chat notification
  Packet notify;
  ASSERT_TRUE(DecodeFramed(receiver->sent[0], &notify));
  EXPECT_EQ(notify.msg_id(), chirp::gateway::CHAT_MESSAGE_NOTIFY);
  chirp::chat::ChatMessage msg;
  ASSERT_TRUE(msg.ParseFromString(notify.body()));
  EXPECT_EQ(msg.content(), "hi bob");
  EXPECT_EQ(msg.channel_id(), "alice|bob");
}

TEST_F(DistributedInternalsTest, HandleSendMessageGroupUsesChannelAndBroadcast) {
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  auto sender = std::make_shared<MockSession>();

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(chirp::chat::TEAM);
  req.set_channel_id("team-9");
  req.set_content("hello team");

  HandleSendMessage(req, sender, state_, store_, router, push_, 1);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(sender->sent[0], &pkt));
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(DistributedInternalsTest, MessageStoreCallsRedis) {
  std::vector<std::vector<std::string>> cmds;
  std::mutex mu;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mu);
    cmds.push_back(args);
    if (!args.empty() && args[0] == "LRANGE") {
      return chirp_test::Array({"m1", "m2"});
    }
    return chirp_test::Simple("OK");
  });

  store_->redis = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake.port());
  store_->offline_ttl_seconds = 99;

  store_->AddOffline("bob", "msg1");
  store_->AddToHistory("chan", "h1");

  auto popped = store_->PopOffline("bob");
  ASSERT_EQ(popped.size(), 2u);
  EXPECT_EQ(popped[0], "m1");

  auto hist = store_->GetHistory("chan", 0);  // limit<=0 -> default 50
  ASSERT_EQ(hist.size(), 2u);

  // Verify the commands seen by the fake (2 per RedisClient call: value op + expire)
  std::vector<std::string> firsts;
  {
    std::lock_guard<std::mutex> lock(mu);
    for (auto& c : cmds) {
      if (!c.empty()) firsts.push_back(c[0] + " " + (c.size() > 1 ? c[1] : ""));
    }
  }
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "RPUSH chirp:chat:offline:bob"), firsts.end());
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "EXPIRE chirp:chat:offline:bob"), firsts.end());
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "LRANGE chirp:chat:offline:bob"), firsts.end());
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "DEL chirp:chat:offline:bob"), firsts.end());
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "RPUSH chirp:chat:history:chan"), firsts.end());
  EXPECT_NE(std::find(firsts.begin(), firsts.end(), "LRANGE chirp:chat:history:chan"), firsts.end());
}

TEST_F(DistributedInternalsTest, HandleLoginDeliversOfflineMessages) {
  chirp::chat::ChatMessage offline_msg;
  offline_msg.set_message_id("m1");
  offline_msg.set_sender_id("carol");
  offline_msg.set_content("offline hello");

  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "LRANGE") {
      return chirp_test::Array({offline_msg.SerializeAsString()});
    }
    return chirp_test::Simple("OK");
  });

  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  store_->redis = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake.port());
  state_->instance_id = "inst";

  auto session = std::make_shared<MockSession>();
  chirp::auth::LoginRequest req;
  req.set_token("alice");
  HandleLogin(req, session, state_, store_, router, &verifier_, 1);

  // LOGIN_RESP goes out first (clients read exactly one frame as "the login
  // response"), then the offline refill notifys.
  ASSERT_EQ(session->sent.size(), 2u);
  Packet resp_pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &resp_pkt));
  EXPECT_EQ(resp_pkt.msg_id(), chirp::gateway::LOGIN_RESP);

  Packet notify;
  ASSERT_TRUE(DecodeFramed(session->sent[1], &notify));
  EXPECT_EQ(notify.msg_id(), chirp::gateway::CHAT_MESSAGE_NOTIFY);
  chirp::chat::ChatMessage got;
  ASSERT_TRUE(got.ParseFromString(notify.body()));
  EXPECT_EQ(got.content(), "offline hello");
}

TEST_F(DistributedInternalsTest, HandleSendMessageOfflineReceiverStored) {
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>&) {
    return chirp_test::Simple("OK");
  });
  asio::io_context io;
  auto router = std::make_shared<chirp::network::MessageRouter>(io, "127.0.0.1", 1);
  store_->redis = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake.port());

  auto sender = std::make_shared<MockSession>();
  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("ghost");  // not logged in anywhere locally
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("store me");

  HandleSendMessage(req, sender, state_, store_, router, push_, 5);

  ASSERT_EQ(sender->sent.size(), 1u);  // OK response only
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(sender->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::SEND_MESSAGE_RESP);
}

TEST_F(DistributedInternalsTest, HandleGetHistoryFiltersBadEntries) {
  chirp::chat::ChatMessage good;
  good.set_message_id("g1");
  good.set_content("valid");

  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "LRANGE") {
      // Fill constructor: the literal "\xff\xff" is only 3 bytes (NUL
      // included), so string(ptr, 4) would read one byte out of bounds —
      // an ASLR-dependent segfault that CI hit in Release builds.
      return chirp_test::Array({good.SerializeAsString(), std::string(4, '\xff')});
    }
    return chirp_test::Simple("OK");
  });
  store_->redis = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake.port());

  auto session = std::make_shared<MockSession>();
  chirp::chat::GetHistoryRequest req;
  req.set_channel_id("c1");
  req.set_limit(10);
  HandleGetHistory(req, session, store_, 2);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::GET_HISTORY_RESP);
  chirp::chat::GetHistoryResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.messages_size(), 1);  // invalid entry dropped
  EXPECT_EQ(resp.messages(0).content(), "valid");
}

TEST_F(DistributedInternalsTest, HandleGetHistoryWithoutRedisSucceeds) {
  auto session = std::make_shared<MockSession>();
  chirp::chat::GetHistoryRequest req;
  req.set_channel_id("c1");
  req.set_limit(10);

  HandleGetHistory(req, session, store_, /*seq=*/7);

  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::GET_HISTORY_RESP);
  chirp::chat::GetHistoryResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.messages_size(), 0);
  EXPECT_FALSE(resp.has_more());
}

// Full round trip against the real distributed chat main(): a TCP client
// logs in, exchanges messages, pulls history, logs out, then SIGTERM stops
// the service. This drives the dispatch lambdas inside main().
//
// Picking a free port with a bind+close probe leaves a small window in which
// the kernel can hand the same port to another process. A lost bind race now
// makes the service exit gracefully (rc == 1), so the test retries on a
// fresh port instead of failing.
TEST(DistributedMainTest, EndToEndClientSession) {
  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    // Pick two free ports up front (bind+close), then hand them to the
    // service. They must be probed independently: the kernel hands out
    // ephemeral ports sequentially, so port+1 is exactly what the next
    // outgoing connection (e.g. the dead-Redis reconnect loop) grabs.
    uint16_t port = 0;
    uint16_t ws_port = 0;
    {
      asio::io_context probe_io;
      asio::ip::tcp::acceptor probe(
          probe_io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
      port = static_cast<uint16_t>(probe.local_endpoint().port());
      asio::ip::tcp::acceptor ws_probe(
          probe_io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
      ws_port = static_cast<uint16_t>(ws_probe.local_endpoint().port());
    }

    const std::string port_str = std::to_string(port);
    const std::string ws_port_str = std::to_string(ws_port);
    std::vector<std::string> args = {
        "chat", "--port", port_str, "--ws_port", ws_port_str,
        "--redis_host", "127.0.0.1", "--redis_port", "1",
        "--instance_id", "e2e-instance"};
    std::vector<std::unique_ptr<char[]>> holds;
    std::vector<char*> argv;
    for (auto& a : args) {
      auto buf = std::make_unique<char[]>(a.size() + 1);
      std::memcpy(buf.get(), a.c_str(), a.size() + 1);
      argv.push_back(buf.get());
      holds.push_back(std::move(buf));
    }

    std::atomic<int> rc{12345};
    std::thread runner([&] {
      rc = chirp_chat_distributed_main(static_cast<int>(argv.size()), argv.data());
    });

    // The service io runs on the runner thread; the client uses its own io.
    asio::io_context client_io;
    chirp::network::TcpClient client(client_io);
    std::vector<Packet> received;
    std::mutex rx_mu;
    client.SetCallbacks(
        [&](std::shared_ptr<chirp::network::Session>, std::string&& payload) {
          Packet pkt;
          if (pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            std::lock_guard<std::mutex> lock(rx_mu);
            received.push_back(pkt);
          }
        },
        [](std::shared_ptr<chirp::network::Session>) {});

    bool connected = false;
    bool service_exited = false;
    for (int i = 0; i < 300 && !connected; ++i) {
      connected = client.Connect("127.0.0.1", port);
      if (!connected) {
        if (rc.load() != 12345) {
          // The service exited before accepting: a lost bind race.
          service_exited = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
    if (service_exited) {
      runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }
    ASSERT_TRUE(connected);

    auto SendAndWait = [&](chirp::gateway::MsgID id, int64_t seq,
                           const google::protobuf::Message& body, int expect) {
      Packet pkt;
      pkt.set_msg_id(id);
      pkt.set_sequence(seq);
      pkt.set_body(body.SerializeAsString());
      std::string framed(4 + pkt.ByteSizeLong(), '\0');
      const uint32_t len = static_cast<uint32_t>(pkt.ByteSizeLong());
      framed[0] = static_cast<char>((len >> 24) & 0xFF);
      framed[1] = static_cast<char>((len >> 16) & 0xFF);
      framed[2] = static_cast<char>((len >> 8) & 0xFF);
      framed[3] = static_cast<char>(len & 0xFF);
      pkt.SerializeToArray(framed.data() + 4, static_cast<int>(len));
      client.GetSession()->Send(framed);
      for (int i = 0; i < 400; i++) {
        client_io.poll();
        client_io.restart();
        {
          std::lock_guard<std::mutex> lock(rx_mu);
          if (static_cast<int>(received.size()) >= expect) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return false;
    };

    chirp::auth::LoginRequest login;
    login.set_token("e2e-user");
    EXPECT_TRUE(SendAndWait(chirp::gateway::LOGIN_REQ, 1, login, 1));

    chirp::chat::SendMessageRequest send;
    send.set_sender_id("e2e-user");
    send.set_receiver_id("other");
    send.set_channel_type(chirp::chat::PRIVATE);
    send.set_content("hello");
    EXPECT_TRUE(SendAndWait(chirp::gateway::SEND_MESSAGE_REQ, 2, send, 2));

    chirp::chat::GetHistoryRequest hist;
    hist.set_channel_id("e2e-user|other");
    hist.set_limit(10);
    EXPECT_TRUE(SendAndWait(chirp::gateway::GET_HISTORY_REQ, 3, hist, 3));

    chirp::auth::LogoutRequest logout;
    logout.set_user_id("e2e-user");
    EXPECT_TRUE(SendAndWait(chirp::gateway::LOGOUT_REQ, 4, logout, 4));

    {
      std::lock_guard<std::mutex> lock(rx_mu);
      ASSERT_EQ(received.size(), 4u);
      EXPECT_EQ(received[0].msg_id(), chirp::gateway::LOGIN_RESP);
      EXPECT_EQ(received[1].msg_id(), chirp::gateway::SEND_MESSAGE_RESP);
      EXPECT_EQ(received[2].msg_id(), chirp::gateway::GET_HISTORY_RESP);
      EXPECT_EQ(received[3].msg_id(), chirp::gateway::LOGOUT_RESP);
    }
    client.Disconnect();

    raise(SIGTERM);
    for (int i = 0; i < 500 && rc.load() == 12345; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_NE(rc.load(), 12345);
    EXPECT_EQ(rc.load(), 0);
    runner.join();
    return;
  }
}

// A bind loss (port already taken) must make the service exit gracefully
// with a non-zero code instead of letting the system_error abort the whole
// process.
TEST(DistributedMainTest, BindFailureExitsGracefully) {
  // Hold both target ports for the entire call.
  asio::io_context probe_io;
  asio::ip::tcp::acceptor probe(
      probe_io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const uint16_t port = probe.local_endpoint().port();

  std::vector<std::string> args = {
      "chat", "--port", std::to_string(port), "--ws_port", std::to_string(port + 1),
      "--redis_host", "127.0.0.1", "--redis_port", "1",
      "--instance_id", "bind-fail-instance"};
  std::vector<std::unique_ptr<char[]>> holds;
  std::vector<char*> argv;
  for (auto& a : args) {
    auto buf = std::make_unique<char[]>(a.size() + 1);
    std::memcpy(buf.get(), a.c_str(), a.size() + 1);
    argv.push_back(buf.get());
    holds.push_back(std::move(buf));
  }

  std::atomic<int> rc{12345};
  std::thread runner([&] {
    rc = chirp_chat_distributed_main(static_cast<int>(argv.size()), argv.data());
  });
  for (int i = 0; i < 500 && rc.load() == 12345; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(rc.load(), 1);
  runner.join();
}

TEST_F(ReadReceiptManagerTest, GetUnreadCountWithCursorsSumsChannels) {
  mgr_.MarkRead("alice", "c1", chirp::chat::PRIVATE, "m1", 1000);
  mgr_.MarkRead("alice", "c2", chirp::chat::TEAM, "m2", 2000);
  // Cursor entries exist for both channels; the sum runs over them.
  EXPECT_EQ(mgr_.GetUnreadCount("alice"), 0);
  EXPECT_EQ(mgr_.GetUnreadCount("nobody"), 0);
}

TEST_F(ReactionManagerTest, GetReactionsForMessagesSortsDifferentCounts) {
  // Two emojis with different counts on the same message exercise the
  // descending-order comparator in the bulk path.
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(mgr_.AddReaction("m1", "u" + std::to_string(i), ":fire:", nullptr));
  }
  EXPECT_TRUE(mgr_.AddReaction("m1", "u0", ":tada:", nullptr));

  auto result = mgr_.GetReactionsForMessages({"m1"});
  ASSERT_EQ(result["m1"].size(), 2u);
  EXPECT_EQ(result["m1"][0].emoji(), ":fire:");
  EXPECT_EQ(result["m1"][0].count(), 3);
  EXPECT_EQ(result["m1"][1].emoji(), ":tada:");
  EXPECT_EQ(result["m1"][1].count(), 1);
}

}  // namespace
