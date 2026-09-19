// Prefixed proto namespaces for the decoders below; MsgID/ErrorCode and the
// shared enums come from the barrel (bare export).
import 'dart:typed_data';

import 'package:chirp_proto/proto/auth.pb.dart' as auth;
import 'package:chirp_proto/proto/chat.pb.dart' as chat;
import 'package:chirp_proto/proto/notification.pb.dart' as notification;
// party.KickMemberRequest/Response collide with chat's and are hidden from
// the barrel; the party file comes in directly, prefixed.
import 'package:chirp_proto/proto/party.pb.dart' as party;
import 'package:chirp_proto/proto/social.pb.dart' as social;
import 'package:chirp_proto/chirp_proto.dart';
import 'package:protobuf/protobuf.dart';

/// A typed request/response pair. On this protocol a response is correlated
/// by Packet.sequence, not by msgId — the server answers with the RESP msgId
/// and echoes the request's sequence. The spec keeps both ids and the
/// decoder in one place so call sites never touch raw msgIds.
class MessageSpec<TResp extends GeneratedMessage> {
  const MessageSpec(this.reqMsgId, this.respMsgId, this.decodeResponse);

  final MsgID reqMsgId;
  final MsgID respMsgId;

  /// Protoc-plugin constructor tear-off, e.g. `auth.LoginResponse.fromBuffer`
  /// (its List<int> parameter accepts Uint8List arguments).
  final TResp Function(Uint8List bytes) decodeResponse;
}

// Core pair used by the connection layer itself.
const login = MessageSpec<auth.LoginResponse>(
    MsgID.LOGIN_REQ, MsgID.LOGIN_RESP, auth.LoginResponse.fromBuffer);
const logout = MessageSpec<auth.LogoutResponse>(
    MsgID.LOGOUT_REQ, MsgID.LOGOUT_RESP, auth.LogoutResponse.fromBuffer);

// Chat (direct chat WS entry).
const sendMessage = MessageSpec<chat.SendMessageResponse>(
    MsgID.SEND_MESSAGE_REQ,
    MsgID.SEND_MESSAGE_RESP,
    chat.SendMessageResponse.fromBuffer);
const getHistory = MessageSpec<chat.GetHistoryResponse>(MsgID.GET_HISTORY_REQ,
    MsgID.GET_HISTORY_RESP, chat.GetHistoryResponse.fromBuffer);
const markRead = MessageSpec<chat.MarkReadResponse>(MsgID.MARK_READ_REQ,
    MsgID.MARK_READ_RESP, chat.MarkReadResponse.fromBuffer);
const getUserGroups = MessageSpec<chat.GetUserGroupsResponse>(
    MsgID.GET_USER_GROUPS_REQ,
    MsgID.GET_USER_GROUPS_RESP,
    chat.GetUserGroupsResponse.fromBuffer);
const createGroup = MessageSpec<chat.CreateGroupResponse>(
    MsgID.CREATE_GROUP_REQ,
    MsgID.CREATE_GROUP_RESP,
    chat.CreateGroupResponse.fromBuffer);
const inviteToGroup = MessageSpec<chat.InviteToGroupResponse>(
    MsgID.INVITE_TO_GROUP_REQ,
    MsgID.INVITE_TO_GROUP_RESP,
    chat.InviteToGroupResponse.fromBuffer);
const leaveGroup = MessageSpec<chat.LeaveGroupResponse>(MsgID.LEAVE_GROUP_REQ,
    MsgID.LEAVE_GROUP_RESP, chat.LeaveGroupResponse.fromBuffer);
const kickMember = MessageSpec<chat.KickMemberResponse>(MsgID.KICK_MEMBER_REQ,
    MsgID.KICK_MEMBER_RESP, chat.KickMemberResponse.fromBuffer);
const getGroupMembers = MessageSpec<chat.GetGroupMembersResponse>(
    MsgID.GET_GROUP_MEMBERS_REQ,
    MsgID.GET_GROUP_MEMBERS_RESP,
    chat.GetGroupMembersResponse.fromBuffer);

// Reactions, edit/delete (typing has no REQ: it is fire-and-forget 2208).
const addReaction = MessageSpec<chat.AddReactionResponse>(
    MsgID.ADD_REACTION_REQ,
    MsgID.ADD_REACTION_RESP,
    chat.AddReactionResponse.fromBuffer);
const removeReaction = MessageSpec<chat.RemoveReactionResponse>(
    MsgID.REMOVE_REACTION_REQ,
    MsgID.REMOVE_REACTION_RESP,
    chat.RemoveReactionResponse.fromBuffer);
const editMessage = MessageSpec<chat.EditMessageResponse>(
    MsgID.EDIT_MESSAGE_REQ,
    MsgID.EDIT_MESSAGE_RESP,
    chat.EditMessageResponse.fromBuffer);
const deleteMessage = MessageSpec<chat.DeleteMessageResponse>(
    MsgID.DELETE_MESSAGE_REQ,
    MsgID.DELETE_MESSAGE_RESP,
    chat.DeleteMessageResponse.fromBuffer);

// Social plane (WS 8001): friends and presence.
const addFriend = MessageSpec<social.AddFriendResponse>(MsgID.ADD_FRIEND_REQ,
    MsgID.ADD_FRIEND_RESP, social.AddFriendResponse.fromBuffer);
const friendRequestAction = MessageSpec<social.FriendRequestActionResponse>(
    MsgID.FRIEND_REQUEST_ACTION_REQ,
    MsgID.FRIEND_REQUEST_ACTION_RESP,
    social.FriendRequestActionResponse.fromBuffer);
const removeFriend = MessageSpec<social.RemoveFriendResponse>(
    MsgID.REMOVE_FRIEND_REQ,
    MsgID.REMOVE_FRIEND_RESP,
    social.RemoveFriendResponse.fromBuffer);
const getFriendList = MessageSpec<social.GetFriendListResponse>(
    MsgID.GET_FRIEND_LIST_REQ,
    MsgID.GET_FRIEND_LIST_RESP,
    social.GetFriendListResponse.fromBuffer);
const getPendingRequests = MessageSpec<social.GetPendingRequestsResponse>(
    MsgID.GET_PENDING_REQUESTS_REQ,
    MsgID.GET_PENDING_REQUESTS_RESP,
    social.GetPendingRequestsResponse.fromBuffer);
const blockUser = MessageSpec<social.BlockUserResponse>(MsgID.BLOCK_USER_REQ,
    MsgID.BLOCK_USER_RESP, social.BlockUserResponse.fromBuffer);
const unblockUser = MessageSpec<social.UnblockUserResponse>(
    MsgID.UNBLOCK_USER_REQ,
    MsgID.UNBLOCK_USER_RESP,
    social.UnblockUserResponse.fromBuffer);
const getBlockedList = MessageSpec<social.GetBlockedListResponse>(
    MsgID.GET_BLOCKED_LIST_REQ,
    MsgID.GET_BLOCKED_LIST_RESP,
    social.GetBlockedListResponse.fromBuffer);
const setPresence = MessageSpec<social.SetPresenceResponse>(
    MsgID.SET_PRESENCE_REQ,
    MsgID.SET_PRESENCE_RESP,
    social.SetPresenceResponse.fromBuffer);
const getPresence = MessageSpec<social.GetPresenceResponse>(
    MsgID.GET_PRESENCE_REQ,
    MsgID.GET_PRESENCE_RESP,
    social.GetPresenceResponse.fromBuffer);

// Party plane (WS 7501): cross-game team-up. Invite-accept only, snapshot
// sync (PARTY_STATE_CHANGED carries the full PartyInfo to every member).
const createParty = MessageSpec<party.CreatePartyResponse>(
    MsgID.CREATE_PARTY_REQ,
    MsgID.CREATE_PARTY_RESP,
    party.CreatePartyResponse.fromBuffer);
const disbandParty = MessageSpec<party.DisbandPartyResponse>(
    MsgID.DISBAND_PARTY_REQ,
    MsgID.DISBAND_PARTY_RESP,
    party.DisbandPartyResponse.fromBuffer);
const inviteToParty = MessageSpec<party.InviteToPartyResponse>(
    MsgID.INVITE_TO_PARTY_REQ,
    MsgID.INVITE_TO_PARTY_RESP,
    party.InviteToPartyResponse.fromBuffer);
const acceptPartyInvite = MessageSpec<party.AcceptInviteResponse>(
    MsgID.ACCEPT_INVITE_REQ,
    MsgID.ACCEPT_INVITE_RESP,
    party.AcceptInviteResponse.fromBuffer);
const declinePartyInvite = MessageSpec<party.DeclineInviteResponse>(
    MsgID.DECLINE_INVITE_REQ,
    MsgID.DECLINE_INVITE_RESP,
    party.DeclineInviteResponse.fromBuffer);
const leaveParty = MessageSpec<party.LeavePartyResponse>(MsgID.LEAVE_PARTY_REQ,
    MsgID.LEAVE_PARTY_RESP, party.LeavePartyResponse.fromBuffer);
const kickPartyMember = MessageSpec<party.KickMemberResponse>(
    MsgID.KICK_PARTY_MEMBER_REQ,
    MsgID.KICK_PARTY_MEMBER_RESP,
    party.KickMemberResponse.fromBuffer);
const transferPartyLeader = MessageSpec<party.TransferLeaderResponse>(
    MsgID.TRANSFER_LEADER_REQ,
    MsgID.TRANSFER_LEADER_RESP,
    party.TransferLeaderResponse.fromBuffer);
const setPartyReady = MessageSpec<party.SetReadyResponse>(MsgID.SET_READY_REQ,
    MsgID.SET_READY_RESP, party.SetReadyResponse.fromBuffer);
const getMyParty = MessageSpec<party.GetMyPartyResponse>(MsgID.GET_MY_PARTY_REQ,
    MsgID.GET_MY_PARTY_RESP, party.GetMyPartyResponse.fromBuffer);

// Device plane (app_gateway WS 5201): registration / listing of the push
// targets for our account. app_gateway authenticates the session and pins
// user_id server-side; 6009 PUSH_NOTIFICATION is deliberately absent — the
// edge refuses it from clients.
const registerDevice = MessageSpec<notification.RegisterDeviceResponse>(
    MsgID.REGISTER_DEVICE_REQ,
    MsgID.REGISTER_DEVICE_RESP,
    notification.RegisterDeviceResponse.fromBuffer);
const unregisterDevice = MessageSpec<notification.UnregisterDeviceResponse>(
    MsgID.UNREGISTER_DEVICE_REQ,
    MsgID.UNREGISTER_DEVICE_RESP,
    notification.UnregisterDeviceResponse.fromBuffer);
const getUserDevices = MessageSpec<notification.GetUserDevicesResponse>(
    MsgID.GET_USER_DEVICES_REQ,
    MsgID.GET_USER_DEVICES_RESP,
    notification.GetUserDevicesResponse.fromBuffer);
