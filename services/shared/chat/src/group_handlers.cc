#include "group_handlers.h"

#include <algorithm>

#include "runtime_utils.h"

namespace chirp {
namespace chat {

namespace {

bool SameUser(std::string_view authenticated_user_id, const std::string& claimed) {
  return !claimed.empty() && claimed == authenticated_user_id;
}

bool RoleAtLeast(const std::vector<chirp::chat::GroupMember>& members,
                 const std::string& user_id, chirp::chat::GroupMemberRole role) {
  for (const auto& member : members) {
    if (member.user_id() == user_id) {
      return member.role() >= role;
    }
  }
  return false;
}

std::vector<chirp::chat::GroupMember> PageMembers(
    std::vector<chirp::chat::GroupMember> members, int32_t limit, int32_t offset) {
  if (offset > 0) {
    members.erase(members.begin(),
                  members.begin() + std::min<int32_t>(offset, static_cast<int32_t>(members.size())));
  }
  if (limit > 0 && static_cast<int32_t>(members.size()) > limit) {
    members.resize(static_cast<size_t>(limit));
  }
  return members;
}

}  // namespace

GroupHandlers::GroupHandlers(GroupManager& groups, GroupMemberNotifier notify)
    : groups_(groups), notify_(std::move(notify)) {}

chirp::chat::CreateGroupResponse GroupHandlers::HandleCreateGroup(
    const chirp::chat::CreateGroupRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::CreateGroupResponse resp;

  if (req.group_name().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!req.creator_id().empty() && !SameUser(authenticated_user_id, req.creator_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  const std::string creator_id(authenticated_user_id);
  resp.set_group_id(groups_.CreateGroup(creator_id, req.group_name(), req.description(),
                                        req.avatar_url(), req.max_members(),
                                        std::vector<std::string>(req.initial_members().begin(),
                                                                 req.initial_members().end())));
  resp.set_code(chirp::common::OK);

  chirp::chat::GroupInfo info;
  if (groups_.GetGroup(resp.group_id(), &info)) {
    chirp::chat::GroupCreatedNotify notify;
    *notify.mutable_group() = info;
    notify.set_timestamp(chirp::chat::runtime::NowMs());
    NotifyMembers(resp.group_id(), chirp::gateway::GROUP_CREATED_NOTIFY, notify);
  }
  return resp;
}

chirp::chat::JoinGroupResponse GroupHandlers::HandleJoinGroup(
    const chirp::chat::JoinGroupRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::JoinGroupResponse resp;

  if (req.group_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  if (!groups_.GetGroup(req.group_id(), resp.mutable_group())) {
    resp.clear_group();
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  if (groups_.IsMember(req.group_id(), req.user_id())) {
    // Re-joining (e.g. after a reconnect) is a no-op, not an error.
    resp.set_code(chirp::common::OK);
    return resp;
  }
  if (!groups_.AddMember(req.group_id(), req.user_id())) {
    // The only failure mode left after the lookups above is a full group.
    resp.clear_group();
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  resp.set_code(chirp::common::OK);
  chirp::chat::GroupMember member;
  member.set_user_id(req.user_id());
  member.set_role(chirp::chat::MEMBER);
  chirp::chat::GroupMemberJoinedNotify notify;
  notify.set_group_id(req.group_id());
  *notify.mutable_member() = member;
  notify.set_timestamp(chirp::chat::runtime::NowMs());
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_JOINED_NOTIFY, notify);
  return resp;
}

chirp::chat::LeaveGroupResponse GroupHandlers::HandleLeaveGroup(
    const chirp::chat::LeaveGroupRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::LeaveGroupResponse resp;

  if (req.group_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  chirp::chat::GroupInfo group_info;
  if (!groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  if (!groups_.IsMember(req.group_id(), req.user_id())) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  groups_.RemoveMember(req.group_id(), req.user_id());

  chirp::chat::GroupMemberLeftNotify notify;
  notify.set_group_id(req.group_id());
  notify.set_user_id(req.user_id());
  notify.set_timestamp(chirp::chat::runtime::NowMs());
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_LEFT_NOTIFY, notify);

  resp.set_code(chirp::common::OK);
  return resp;
}

chirp::chat::KickMemberResponse GroupHandlers::HandleKickMember(
    const chirp::chat::KickMemberRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::KickMemberResponse resp;

  if (req.group_id().empty() || req.target_user_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  const std::string requester_id(authenticated_user_id);
  if (!req.requester_id().empty() && !SameUser(authenticated_user_id, req.requester_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::GroupInfo group_info;
  if (!groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  const std::vector<chirp::chat::GroupMember> members = groups_.GetMembers(req.group_id());
  const bool is_owner = group_info.owner_id() == requester_id;
  // Moderators and up may kick regular members.
  if (!is_owner && !RoleAtLeast(members, requester_id, chirp::chat::MODERATOR)) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  if (!groups_.IsMember(req.group_id(), req.target_user_id())) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }

  groups_.RemoveMember(req.group_id(), req.target_user_id());

  chirp::chat::GroupMemberKickedNotify notify;
  notify.set_group_id(req.group_id());
  notify.set_user_id(req.target_user_id());
  notify.set_kicked_by(requester_id);
  notify.set_timestamp(chirp::chat::runtime::NowMs());
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_KICKED_NOTIFY, notify);

  resp.set_code(chirp::common::OK);
  return resp;
}

chirp::chat::GetGroupInfoResponse GroupHandlers::HandleGetGroupInfo(
    const chirp::chat::GetGroupInfoRequest& req, std::string_view /*authenticated_user_id*/) {
  chirp::chat::GetGroupInfoResponse resp;
  if (req.group_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!groups_.GetGroup(req.group_id(), resp.mutable_group())) {
    resp.clear_group();
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  resp.set_code(chirp::common::OK);
  return resp;
// GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
}

chirp::chat::GetGroupMembersResponse GroupHandlers::HandleGetGroupMembers(
    const chirp::chat::GetGroupMembersRequest& req, std::string_view /*authenticated_user_id*/) {
  chirp::chat::GetGroupMembersResponse resp;
  if (req.group_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  auto members = groups_.GetMembers(req.group_id());
  chirp::chat::GroupInfo group_info;
  if (members.empty() && !groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  resp.set_total_count(static_cast<int32_t>(members.size()));
  for (auto& member : PageMembers(std::move(members), req.limit(), req.offset())) {
    *resp.add_members() = std::move(member);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

chirp::chat::GetUserGroupsResponse GroupHandlers::HandleGetUserGroups(
    const chirp::chat::GetUserGroupsRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::GetUserGroupsResponse resp;
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  auto groups = groups_.GetUserGroups(req.user_id());
  resp.set_total_count(static_cast<int32_t>(groups.size()));
  if (req.offset() > 0) {
    groups.erase(groups.begin(),
                 groups.begin() + std::min<int32_t>(req.offset(), static_cast<int32_t>(groups.size())));
  }
  if (req.limit() > 0 && static_cast<int32_t>(groups.size()) > req.limit()) {
    groups.resize(static_cast<size_t>(req.limit()));
  }
  for (auto& info : groups) {
    *resp.add_groups() = std::move(info);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}

chirp::chat::InviteToGroupResponse GroupHandlers::HandleInviteToGroup(
    const chirp::chat::InviteToGroupRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::InviteToGroupResponse resp;

  if (req.group_id().empty() || req.target_user_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.inviter_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  chirp::chat::GroupInfo group_info;
  if (!groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  if (!groups_.IsMember(req.group_id(), req.inviter_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  if (groups_.IsMember(req.group_id(), req.target_user_id())) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!groups_.AddMember(req.group_id(), req.target_user_id())) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  chirp::chat::GroupMember member;
  member.set_user_id(req.target_user_id());
  member.set_role(chirp::chat::MEMBER);
  chirp::chat::GroupMemberJoinedNotify notify;
  notify.set_group_id(req.group_id());
  *notify.mutable_member() = member;
  notify.set_timestamp(chirp::chat::runtime::NowMs());
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_JOINED_NOTIFY, notify);

  resp.set_code(chirp::common::OK);
  return resp;
}

chirp::chat::SetMemberAliasResponse GroupHandlers::HandleSetMemberAlias(
    const chirp::chat::SetMemberAliasRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::SetMemberAliasResponse resp;

  if (authenticated_user_id.empty() || req.group_id().empty() || req.target_user_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  // 码点计数口径与 chat_validation 的 MaxContentChars 一致：每个非 UTF-8
  // 续字节都是一个新字符。
  size_t alias_chars = 0;
  for (char c : req.alias()) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      ++alias_chars;
    }
  }
  if (alias_chars > GroupManager::kMaxAliasCodePoints) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  const std::string operator_id(authenticated_user_id);
  chirp::chat::GroupInfo group_info;
  if (!groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  if (!groups_.IsMember(req.group_id(), req.target_user_id())) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }

  const std::vector<chirp::chat::GroupMember> members = groups_.GetMembers(req.group_id());
  const bool self = req.target_user_id() == operator_id;
  if (!self && !RoleAtLeast(members, operator_id, chirp::chat::MODERATOR)) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  groups_.SetMemberAlias(req.group_id(), req.target_user_id(), req.alias());

  resp.set_code(chirp::common::OK);
  resp.set_group_id(req.group_id());
  resp.set_user_id(req.target_user_id());
  resp.set_alias(req.alias());

  chirp::chat::GroupMemberAliasUpdatedNotify notify;
  notify.set_group_id(req.group_id());
  notify.set_user_id(req.target_user_id());
  notify.set_alias(req.alias());
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_ALIAS_UPDATED_NOTIFY, notify);
  return resp;
}

chirp::chat::SetGroupMuteResponse GroupHandlers::HandleSetGroupMute(
    const chirp::chat::SetGroupMuteRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::SetGroupMuteResponse resp;

  if (authenticated_user_id.empty() || req.group_id().empty() || req.target_user_id().empty() ||
      req.duration_sec() < 0) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  // game_chat_features「按分钟/小时/天」：30 天封顶，既挡荒谬值也保
  // until_ms = now + duration*1000 不溢出 int64。
  constexpr int64_t kMaxMuteDurationSec = 30LL * 24 * 60 * 60;
  if (req.duration_sec() > kMaxMuteDurationSec) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }

  const std::string operator_id(authenticated_user_id);
  chirp::chat::GroupInfo group_info;
  if (!groups_.GetGroup(req.group_id(), &group_info)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  if (!groups_.IsMember(req.group_id(), req.target_user_id())) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }
  // 禁言是管理动作，没有"本人禁言自己"的自助分支（区别于 2122 alias）。
  const std::vector<chirp::chat::GroupMember> members = groups_.GetMembers(req.group_id());
  if (!RoleAtLeast(members, operator_id, chirp::chat::MODERATOR)) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  const int64_t now_ms = chirp::chat::runtime::NowMs();
  const int64_t until_ms =
      req.duration_sec() == 0 ? 0 : now_ms + req.duration_sec() * 1000;
  groups_.SetMemberMute(req.group_id(), req.target_user_id(), until_ms);

  resp.set_code(chirp::common::OK);
  resp.set_group_id(req.group_id());
  resp.set_user_id(req.target_user_id());
  resp.set_muted_until_ts(until_ms);

  chirp::chat::GroupMemberMutedNotify notify;
  notify.set_group_id(req.group_id());
  notify.set_user_id(req.target_user_id());
  notify.set_muted_until_ts(until_ms);
  notify.set_operator_id(operator_id);
  notify.set_timestamp(now_ms);
  NotifyMembers(req.group_id(), chirp::gateway::GROUP_MEMBER_MUTED_NOTIFY, notify);
  return resp;
}

std::vector<std::string> GroupHandlers::BroadcastGroupMessage(
    const std::string& group_id, const std::string& sender_id,
    const chirp::chat::ChatMessage& msg) {
  std::vector<std::string> offline;
  for (const auto& member : groups_.GetMembers(group_id)) {
    if (member.user_id() == sender_id) {
      continue;
    }
    if (!notify_(member.user_id(), chirp::gateway::CHAT_MESSAGE_NOTIFY, msg)) {
      offline.push_back(member.user_id());
    }
  }
  return offline;
// GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
}

void GroupHandlers::NotifyMembers(const std::string& group_id,
                                  chirp::gateway::MsgID msg_id,
                                  const google::protobuf::Message& body) {
  for (const auto& member : groups_.GetMembers(group_id)) {
    notify_(member.user_id(), msg_id, body);
  }
}

}  // namespace chat
}  // namespace chirp
