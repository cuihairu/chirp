// Tests for the group request handlers that wire GroupManager into the
// chat packet dispatch. The delivery sink is a recording lambda, so the
// handlers are exercised without any network machinery.
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "group_handlers.h"

#include "proto/common.pb.h"

namespace {

struct NotificationRecord {
  std::string user_id;
  chirp::gateway::MsgID msg_id;
  std::string body;
};

class GroupHandlersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    notifier_ = [this](const std::string& user_id, chirp::gateway::MsgID msg_id,
                       const google::protobuf::Message& body) {
      notifications_.push_back({user_id, msg_id, body.SerializeAsString()});
      return offline_users_.count(user_id) == 0;
    };
    handlers_ = std::make_unique<chirp::chat::GroupHandlers>(groups_, notifier_);
  }

  chirp::chat::GroupInfo MakeGroup(const std::string& owner,
                                   const std::vector<std::string>& initial = {},
                                   int32_t max_members = 0) {
    chirp::chat::CreateGroupRequest req;
    req.set_creator_id(owner);
    req.set_group_name("g");
    req.set_max_members(max_members);
    for (const auto& member : initial) {
      req.add_initial_members(member);
    }
    const chirp::chat::CreateGroupResponse resp =
        handlers_->HandleCreateGroup(req, owner);
    EXPECT_EQ(resp.code(), chirp::common::OK);
    return MakeGroupInfo(resp.group_id());
  }

  chirp::chat::GroupInfo MakeGroupInfo(const std::string& group_id) {
    chirp::chat::GroupInfo info;
    EXPECT_TRUE(groups_.GetGroup(group_id, &info));
    return info;
  }

  int CountNotifications(const std::string& user_id, chirp::gateway::MsgID msg_id) const {
    int count = 0;
    for (const auto& record : notifications_) {
      if (record.user_id == user_id && record.msg_id == msg_id) {
        ++count;
      }
    }
    return count;
  }

  std::set<std::string> offline_users_;
  std::vector<NotificationRecord> notifications_;
  chirp::chat::GroupManager groups_;
  chirp::chat::GroupMemberNotifier notifier_;
  std::unique_ptr<chirp::chat::GroupHandlers> handlers_;
};

TEST_F(GroupHandlersTest, CreateGroupRejectsEmptyName) {
  chirp::chat::CreateGroupRequest req;
  req.set_creator_id("alice");
  const auto resp = handlers_->HandleCreateGroup(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_TRUE(resp.group_id().empty());
}

TEST_F(GroupHandlersTest, CreateGroupRejectsSpoofedCreator) {
  chirp::chat::CreateGroupRequest req;
  req.set_creator_id("mallory");
  req.set_group_name("g");
  const auto resp = handlers_->HandleCreateGroup(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, CreateGroupNotifiesAllMembers) {
  chirp::chat::CreateGroupRequest req;
  req.set_creator_id("alice");
  req.set_group_name("g");
  req.add_initial_members("bob");
  req.add_initial_members("carl");
  req.add_initial_members("alice");  // duplicate of the creator is ignored
  const auto resp = handlers_->HandleCreateGroup(req, "alice");

  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.group_id().empty());
  EXPECT_EQ(groups_.GetMembers(resp.group_id()).size(), 3u);
  // Everyone gets exactly one created notification.
  for (const char* member : {"alice", "bob", "carl"}) {
    EXPECT_EQ(CountNotifications(member, chirp::gateway::GROUP_CREATED_NOTIFY), 1);
  }
}

TEST_F(GroupHandlersTest, JoinGroupRejectsMissingGroupId) {
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("bob");
  const auto resp = handlers_->HandleJoinGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(GroupHandlersTest, JoinGroupRejectsIdentityMismatch) {
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("mallory");
  req.set_group_id("whatever");
  const auto resp = handlers_->HandleJoinGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, JoinGroupRejectsUnknownGroup) {
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id("missing");
  const auto resp = handlers_->HandleJoinGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, JoinGroupIsIdempotent) {
  const auto group = MakeGroup("alice");
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id());

  ASSERT_EQ(handlers_->HandleJoinGroup(req, "bob").code(), chirp::common::OK);
  notifications_.clear();
  const auto second = handlers_->HandleJoinGroup(req, "bob");

  // A repeated join is a no-op: OK, no extra notification, no role reset.
  EXPECT_EQ(second.code(), chirp::common::OK);
  EXPECT_TRUE(second.has_group());
  EXPECT_TRUE(notifications_.empty());
  EXPECT_EQ(groups_.GetMembers(group.group_id()).size(), 2u);
}

TEST_F(GroupHandlersTest, JoinGroupRejectsFullGroup) {
  const auto group = MakeGroup("alice", {}, /*max_members=*/1);
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id());
  const auto resp = handlers_->HandleJoinGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(resp.has_group());
}

TEST_F(GroupHandlersTest, JoinGroupNotifiesMembers) {
  const auto group = MakeGroup("alice");
  chirp::chat::JoinGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id());
  ASSERT_EQ(handlers_->HandleJoinGroup(req, "bob").code(), chirp::common::OK);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::GROUP_MEMBER_JOINED_NOTIFY), 1);
}

TEST_F(GroupHandlersTest, LeaveGroupRejectsMissingGroupId) {
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id("bob");
  const auto resp = handlers_->HandleLeaveGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(GroupHandlersTest, LeaveGroupRejectsIdentityMismatch) {
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id("mallory");
  req.set_group_id("whatever");
  const auto resp = handlers_->HandleLeaveGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, LeaveGroupRejectsUnknownGroup) {
  const auto group = MakeGroup("alice");
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id() + "x");
  const auto resp = handlers_->HandleLeaveGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, LeaveGroupRejectsNonMember) {
  const auto group = MakeGroup("alice");
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id());
  const auto resp = handlers_->HandleLeaveGroup(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(GroupHandlersTest, LeaveGroupNotifiesRemainingMembers) {
  const auto group = MakeGroup("alice", {"bob"});
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id("bob");
  req.set_group_id(group.group_id());
  ASSERT_EQ(handlers_->HandleLeaveGroup(req, "bob").code(), chirp::common::OK);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::GROUP_MEMBER_LEFT_NOTIFY), 1);
  // The departing member is no longer in the group and gets nothing.
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::GROUP_MEMBER_LEFT_NOTIFY), 0);
  EXPECT_FALSE(groups_.IsMember(group.group_id(), "bob"));
}

TEST_F(GroupHandlersTest, KickMemberRejectsMissingParams) {
  chirp::chat::KickMemberRequest req;
  req.set_requester_id("alice");
  const auto resp = handlers_->HandleKickMember(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  chirp::chat::KickMemberRequest no_target;
  no_target.set_requester_id("alice");
  no_target.set_group_id("g1");
  EXPECT_EQ(handlers_->HandleKickMember(no_target, "alice").code(),
            chirp::common::INVALID_PARAM);
}

TEST_F(GroupHandlersTest, KickMemberRejectsRequesterMismatch) {
  chirp::chat::KickMemberRequest req;
  req.set_requester_id("mallory");
  req.set_group_id("g1");
  req.set_target_user_id("bob");
  EXPECT_EQ(handlers_->HandleKickMember(req, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, KickMemberRejectsUnknownGroup) {
  chirp::chat::KickMemberRequest req;
  req.set_group_id("missing");
  req.set_target_user_id("bob");
  EXPECT_EQ(handlers_->HandleKickMember(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, KickMemberRequiresModeratorRole) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  chirp::chat::KickMemberRequest req;
  req.set_group_id(group.group_id());
  req.set_target_user_id("carl");
  // bob is a plain MEMBER and may not kick.
  EXPECT_EQ(handlers_->HandleKickMember(req, "bob").code(),
            chirp::common::AUTH_FAILED);
  EXPECT_TRUE(groups_.IsMember(group.group_id(), "carl"));
}

TEST_F(GroupHandlersTest, KickMemberRejectsUnknownTarget) {
  const auto group = MakeGroup("alice", {"bob"});
  ASSERT_TRUE(groups_.SetMemberRole(group.group_id(), "bob", chirp::chat::MODERATOR));
  chirp::chat::KickMemberRequest req;
  req.set_group_id(group.group_id());
  req.set_target_user_id("stranger");
  EXPECT_EQ(handlers_->HandleKickMember(req, "bob").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, OwnerCanKickMember) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  chirp::chat::KickMemberRequest req;
  req.set_group_id(group.group_id());
  req.set_target_user_id("carl");
  const auto resp = handlers_->HandleKickMember(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(groups_.IsMember(group.group_id(), "carl"));
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::GROUP_MEMBER_KICKED_NOTIFY), 1);
}

TEST_F(GroupHandlersTest, ModeratorCanKickMember) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  ASSERT_TRUE(groups_.SetMemberRole(group.group_id(), "bob", chirp::chat::MODERATOR));
  chirp::chat::KickMemberRequest req;
  req.set_group_id(group.group_id());
  req.set_target_user_id("carl");
  const auto resp = handlers_->HandleKickMember(req, "bob");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(groups_.IsMember(group.group_id(), "carl"));
}

TEST_F(GroupHandlersTest, GetGroupInfoValidatesInput) {
  chirp::chat::GetGroupInfoRequest empty;
  EXPECT_EQ(handlers_->HandleGetGroupInfo(empty, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::GetGroupInfoRequest missing;
  missing.set_group_id("nope");
  EXPECT_EQ(handlers_->HandleGetGroupInfo(missing, "alice").code(),
            chirp::common::USER_NOT_FOUND);

  const auto group = MakeGroup("alice");
  chirp::chat::GetGroupInfoRequest req;
  req.set_group_id(group.group_id());
  const auto resp = handlers_->HandleGetGroupInfo(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.group().group_id(), group.group_id());
  EXPECT_EQ(resp.group().owner_id(), "alice");
}

TEST_F(GroupHandlersTest, GetGroupMembersValidatesInput) {
  chirp::chat::GetGroupMembersRequest empty;
  EXPECT_EQ(handlers_->HandleGetGroupMembers(empty, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::GetGroupMembersRequest missing;
  missing.set_group_id("nope");
  EXPECT_EQ(handlers_->HandleGetGroupMembers(missing, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, GetGroupMembersPaginates) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  chirp::chat::GetGroupMembersRequest req;
  req.set_group_id(group.group_id());
  req.set_offset(1);
  req.set_limit(2);

  const auto resp = handlers_->HandleGetGroupMembers(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.total_count(), 3);
  EXPECT_EQ(resp.members_size(), 2);
}

TEST_F(GroupHandlersTest, GetUserGroupsRejectsIdentityMismatch) {
  chirp::chat::GetUserGroupsRequest req;
  req.set_user_id("mallory");
  EXPECT_EQ(handlers_->HandleGetUserGroups(req, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, GetUserGroupsPaginates) {
  MakeGroup("alice");
  MakeGroup("alice");

  chirp::chat::GetUserGroupsRequest req;
  req.set_user_id("alice");
  req.set_limit(1);
  const auto resp = handlers_->HandleGetUserGroups(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.total_count(), 2);
  EXPECT_EQ(resp.groups_size(), 1);
}

TEST_F(GroupHandlersTest, GetUserGroupsAppliesOffset) {
  MakeGroup("alice");
  MakeGroup("alice");

  chirp::chat::GetUserGroupsRequest req;
  req.set_user_id("alice");
  req.set_offset(1);
  const auto resp = handlers_->HandleGetUserGroups(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.total_count(), 2);
  EXPECT_EQ(resp.groups_size(), 1);
}

TEST_F(GroupHandlersTest, InviteValidatesInput) {
  chirp::chat::InviteToGroupRequest missing_fields;
  missing_fields.set_inviter_id("alice");
  EXPECT_EQ(handlers_->HandleInviteToGroup(missing_fields, "alice").code(),
            chirp::common::INVALID_PARAM);

  const auto group = MakeGroup("alice");
  chirp::chat::InviteToGroupRequest spoofed;
  spoofed.set_inviter_id("mallory");
  spoofed.set_group_id(group.group_id());
  spoofed.set_target_user_id("carl");
  EXPECT_EQ(handlers_->HandleInviteToGroup(spoofed, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::InviteToGroupRequest unknown_group;
  unknown_group.set_inviter_id("alice");
  unknown_group.set_group_id(group.group_id() + "x");
  unknown_group.set_target_user_id("carl");
  EXPECT_EQ(handlers_->HandleInviteToGroup(unknown_group, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(GroupHandlersTest, InviteRejectsNonMemberInviter) {
  const auto group = MakeGroup("alice");
  chirp::chat::InviteToGroupRequest req;
  req.set_inviter_id("bob");
  req.set_group_id(group.group_id());
  req.set_target_user_id("carl");
  EXPECT_EQ(handlers_->HandleInviteToGroup(req, "bob").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(GroupHandlersTest, InviteRejectsExistingMember) {
  const auto group = MakeGroup("alice", {"bob"});
  chirp::chat::InviteToGroupRequest req;
  req.set_inviter_id("alice");
  req.set_group_id(group.group_id());
  req.set_target_user_id("bob");
  EXPECT_EQ(handlers_->HandleInviteToGroup(req, "alice").code(),
            chirp::common::INVALID_PARAM);
}

TEST_F(GroupHandlersTest, InviteNotifiesMembers) {
  const auto group = MakeGroup("alice");
  chirp::chat::InviteToGroupRequest req;
  req.set_inviter_id("alice");
  req.set_group_id(group.group_id());
  req.set_target_user_id("bob");
  const auto resp = handlers_->HandleInviteToGroup(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(groups_.IsMember(group.group_id(), "bob"));
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::GROUP_MEMBER_JOINED_NOTIFY), 1);
}

TEST_F(GroupHandlersTest, InviteFullGroupRejected) {
  // The creator already fills the only slot (max_members=1), so the invite
  // reaches GroupManager::AddMember and is rejected as a full group.
  const auto group = MakeGroup("alice", /*initial=*/{}, /*max_members=*/1);
  chirp::chat::InviteToGroupRequest req;
  req.set_inviter_id("alice");
  req.set_group_id(group.group_id());
  req.set_target_user_id("bob");
  EXPECT_EQ(handlers_->HandleInviteToGroup(req, "alice").code(),
            chirp::common::INVALID_PARAM);
  EXPECT_FALSE(groups_.IsMember(group.group_id(), "bob"));
}

TEST_F(GroupHandlersTest, KickByNonMemberRequesterFails) {
  const auto group = MakeGroup("alice", {"bob"});
  chirp::chat::KickMemberRequest req;
  req.set_group_id(group.group_id());
  req.set_target_user_id("bob");
  req.set_requester_id("carl");
  // carl is neither the owner nor a member: the permission lookup finds no
  // role for him and rejects the kick.
  EXPECT_EQ(handlers_->HandleKickMember(req, "carl").code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(groups_.IsMember(group.group_id(), "bob"));
}

TEST_F(GroupHandlersTest, GetMembersAppliesLimit) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  chirp::chat::GetGroupMembersRequest req;
  req.set_group_id(group.group_id());
  req.set_limit(2);
  const auto resp = handlers_->HandleGetGroupMembers(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.members_size(), 2);
}

TEST_F(GroupHandlersTest, BroadcastExcludesSenderAndQueuesOfflineMembers) {
  const auto group = MakeGroup("alice", {"bob", "carl"});
  offline_users_.insert("carl");

  chirp::chat::ChatMessage msg;
  msg.set_sender_id("alice");
  msg.set_channel_id(group.group_id());
  const auto offline = handlers_->BroadcastGroupMessage(group.group_id(), "alice", msg);

  EXPECT_EQ(offline.size(), 1u);
  EXPECT_EQ(offline[0], "carl");
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::CHAT_MESSAGE_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::CHAT_MESSAGE_NOTIFY), 0);
}

TEST_F(GroupHandlersTest, BroadcastToUnknownGroupDeliversNothing) {
  chirp::chat::ChatMessage msg;
  msg.set_sender_id("alice");
  const auto offline = handlers_->BroadcastGroupMessage("missing", "alice", msg);
  EXPECT_TRUE(offline.empty());
  EXPECT_TRUE(notifications_.empty());
}

TEST_F(GroupHandlersTest, IsMemberReflectsMembership) {
  const auto group = MakeGroup("alice", {"bob"});
  EXPECT_TRUE(handlers_->IsMember(group.group_id(), "alice"));
  EXPECT_TRUE(handlers_->IsMember(group.group_id(), "bob"));
  EXPECT_FALSE(handlers_->IsMember(group.group_id(), "carl"));
}

TEST_F(GroupHandlersTest, SameUserRejectsEmptyClaimedIds) {
  // Empty claimed ids take the !claimed.empty() short-circuit inside
  // SameUser; spoof tests only cover non-empty mismatches.
  chirp::chat::JoinGroupRequest join;
  join.set_group_id("whatever");
  EXPECT_EQ(handlers_->HandleJoinGroup(join, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::LeaveGroupRequest leave;
  leave.set_group_id("whatever");
  EXPECT_EQ(handlers_->HandleLeaveGroup(leave, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::GetUserGroupsRequest groups;
  groups.set_user_id("");
  EXPECT_EQ(handlers_->HandleGetUserGroups(groups, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::InviteToGroupRequest invite;
  invite.set_group_id("g");
  invite.set_target_user_id("bob");
  invite.set_inviter_id("");
  EXPECT_EQ(handlers_->HandleInviteToGroup(invite, "alice").code(),
            chirp::common::AUTH_FAILED);

  // Empty creator_id skips SameUser entirely (the guard is
  // !creator_id.empty() && !SameUser(...)); non-empty + match is the
  // normal create path already covered elsewhere.
  chirp::chat::CreateGroupRequest create;
  create.set_creator_id("");
  create.set_group_name("g");
  EXPECT_EQ(handlers_->HandleCreateGroup(create, "alice").code(),
            chirp::common::OK);
}

}  // namespace
