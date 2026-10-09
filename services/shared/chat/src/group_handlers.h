#ifndef CHIRP_CHAT_GROUP_HANDLERS_H_
#define CHIRP_CHAT_GROUP_HANDLERS_H_

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/message.h>

#include "group_manager.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::chat {

// Delivers one notification packet to a single group member. Returns true
// when the member is online and the payload was handed to its session;
// false means the member is offline and the caller decides whether to
// enqueue the payload.
using GroupMemberNotifier = std::function<bool(
    const std::string& user_id, chirp::gateway::MsgID msg_id,
    const google::protobuf::Message& body)>;

// Wires GroupManager into the packet dispatch shared by every chat runtime.
// Identity checks always run against the authenticated user id from the
// session registry; user-id fields inside request bodies are only trusted
// after they agree with it. ErrorCode has no group-specific values yet, so
// handlers reuse the closest ones: USER_NOT_FOUND for a missing group or
// member, AUTH_FAILED for permission problems, INVALID_PARAM for malformed
// requests (bad names, full groups, duplicate joins).
class GroupHandlers {
 public:
  GroupHandlers(GroupManager& groups, GroupMemberNotifier notify);

  chirp::chat::CreateGroupResponse HandleCreateGroup(
      const chirp::chat::CreateGroupRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::JoinGroupResponse HandleJoinGroup(
      const chirp::chat::JoinGroupRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::LeaveGroupResponse HandleLeaveGroup(
      const chirp::chat::LeaveGroupRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::KickMemberResponse HandleKickMember(
      const chirp::chat::KickMemberRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::GetGroupInfoResponse HandleGetGroupInfo(
      const chirp::chat::GetGroupInfoRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::GetGroupMembersResponse HandleGetGroupMembers(
      const chirp::chat::GetGroupMembersRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::GetUserGroupsResponse HandleGetUserGroups(
      const chirp::chat::GetUserGroupsRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::InviteToGroupResponse HandleInviteToGroup(
      const chirp::chat::InviteToGroupRequest& req,
      std::string_view authenticated_user_id);

  // 群昵称（2122）：本人改自己的，或 MODERATOR+ 改他人的；超 64 码点回
  // INVALID_PARAM，空串清除。成功后向全群推 GROUP_MEMBER_ALIAS_UPDATED_NOTIFY。
  chirp::chat::SetMemberAliasResponse HandleSetMemberAlias(
      const chirp::chat::SetMemberAliasRequest& req,
      std::string_view authenticated_user_id);

  // 群禁言（2250）：MODERATOR+ 禁言/解禁本群成员；duration_sec 上限 30 天，
  // 0 = 解禁。成功后向全群推 GROUP_MEMBER_MUTED_NOTIFY(2252)。
  chirp::chat::SetGroupMuteResponse HandleSetGroupMute(
      const chirp::chat::SetGroupMuteRequest& req,
      std::string_view authenticated_user_id);

  // Fan out one group chat message to every member except the sender.
  // Online members receive CHAT_MESSAGE_NOTIFY through the notifier; the
  // ids of offline members are returned so the transport layer can push
  // the message into their offline queues.
  std::vector<std::string> BroadcastGroupMessage(const std::string& group_id,
                                                 const std::string& sender_id,
                                                 const chirp::chat::ChatMessage& msg);

  bool IsMember(const std::string& group_id, const std::string& user_id) {
    return groups_.IsMember(group_id, user_id);
  }

  // 发送门（SEND_MESSAGE 的 GUILD 分支）用：成员在 now_ms 时刻是否处于
  // 禁言中。false = 未禁言/已到点/不在台账。
  bool IsMuted(const std::string& group_id, const std::string& user_id,
               int64_t now_ms) {
    return groups_.MutedUntil(group_id, user_id, now_ms) > 0;
  }

 private:
  void NotifyMembers(const std::string& group_id, chirp::gateway::MsgID msg_id,
                     const google::protobuf::Message& body);

  GroupManager& groups_;
  GroupMemberNotifier notify_;
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_GROUP_HANDLERS_H_
