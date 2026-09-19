import * as Auth from '@chirp/proto/auth';
import * as Chat from '@chirp/proto/chat';
import * as Party from '@chirp/proto/party';
import * as Social from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';

/**
 * A typed request/response pair. On this protocol a response is correlated by
 * Packet.sequence, not by msgId — the server answers with the RESP msgId and
 * echoes the request's sequence. The spec keeps both ids and the codec in one
 * place so call sites never touch raw msgIds.
 */
export interface MessageSpec<Req, Resp> {
  readonly reqMsgId: MsgID;
  readonly respMsgId: MsgID;
  /** Accepts a partial; unset fields fall back to proto defaults via fromPartial. */
  encodeRequest(message: Partial<Req>): Uint8Array;
  decodeResponse(bytes: Uint8Array): Resp;
}

interface ProtoMessage<T> {
  encode(message: T, writer?: unknown): { finish(): Uint8Array };
  decode(input: Uint8Array, length?: number): T;
  fromPartial(partial: Partial<T>): T;
}

function defineSpec<Req, Resp>(
  reqMsgId: MsgID,
  respMsgId: MsgID,
  Req: ProtoMessage<Req>,
  Resp: ProtoMessage<Resp>,
): MessageSpec<Req, Resp> {
  return {
    reqMsgId,
    respMsgId,
    encodeRequest: (message) => Req.encode(Req.fromPartial(message)).finish(),
    decodeResponse: (bytes) => Resp.decode(bytes),
  };
}

// Core pair used by the connection layer itself.
export const LOGIN = defineSpec(
  MsgID.LOGIN_REQ,
  MsgID.LOGIN_RESP,
  Auth.LoginRequest,
  Auth.LoginResponse,
);
export const LOGOUT = defineSpec(
  MsgID.LOGOUT_REQ,
  MsgID.LOGOUT_RESP,
  Auth.LogoutRequest,
  Auth.LogoutResponse,
);

// Chat (direct chat WS entry; the full table grows in the api layer).
export const SEND_MESSAGE = defineSpec(
  MsgID.SEND_MESSAGE_REQ,
  MsgID.SEND_MESSAGE_RESP,
  Chat.SendMessageRequest,
  Chat.SendMessageResponse,
);
export const GET_HISTORY = defineSpec(
  MsgID.GET_HISTORY_REQ,
  MsgID.GET_HISTORY_RESP,
  Chat.GetHistoryRequest,
  Chat.GetHistoryResponse,
);
export const MARK_READ = defineSpec(
  MsgID.MARK_READ_REQ,
  MsgID.MARK_READ_RESP,
  Chat.MarkReadRequest,
  Chat.MarkReadResponse,
);
export const GET_UNREAD_COUNT = defineSpec(
  MsgID.GET_UNREAD_COUNT_REQ,
  MsgID.GET_UNREAD_COUNT_RESP,
  Chat.GetUnreadCountRequest,
  Chat.GetUnreadCountResponse,
);
export const GET_USER_GROUPS = defineSpec(
  MsgID.GET_USER_GROUPS_REQ,
  MsgID.GET_USER_GROUPS_RESP,
  Chat.GetUserGroupsRequest,
  Chat.GetUserGroupsResponse,
);
export const CREATE_GROUP = defineSpec(
  MsgID.CREATE_GROUP_REQ,
  MsgID.CREATE_GROUP_RESP,
  Chat.CreateGroupRequest,
  Chat.CreateGroupResponse,
);
export const INVITE_TO_GROUP = defineSpec(
  MsgID.INVITE_TO_GROUP_REQ,
  MsgID.INVITE_TO_GROUP_RESP,
  Chat.InviteToGroupRequest,
  Chat.InviteToGroupResponse,
);
export const LEAVE_GROUP = defineSpec(
  MsgID.LEAVE_GROUP_REQ,
  MsgID.LEAVE_GROUP_RESP,
  Chat.LeaveGroupRequest,
  Chat.LeaveGroupResponse,
);
export const KICK_MEMBER = defineSpec(
  MsgID.KICK_MEMBER_REQ,
  MsgID.KICK_MEMBER_RESP,
  Chat.KickMemberRequest,
  Chat.KickMemberResponse,
);
export const GET_GROUP_MEMBERS = defineSpec(
  MsgID.GET_GROUP_MEMBERS_REQ,
  MsgID.GET_GROUP_MEMBERS_RESP,
  Chat.GetGroupMembersRequest,
  Chat.GetGroupMembersResponse,
);

// Reactions, edit/delete (typing has no REQ: it is fire-and-forget 2208).
export const ADD_REACTION = defineSpec(
  MsgID.ADD_REACTION_REQ,
  MsgID.ADD_REACTION_RESP,
  Chat.AddReactionRequest,
  Chat.AddReactionResponse,
);
export const REMOVE_REACTION = defineSpec(
  MsgID.REMOVE_REACTION_REQ,
  MsgID.REMOVE_REACTION_RESP,
  Chat.RemoveReactionRequest,
  Chat.RemoveReactionResponse,
);
export const EDIT_MESSAGE = defineSpec(
  MsgID.EDIT_MESSAGE_REQ,
  MsgID.EDIT_MESSAGE_RESP,
  Chat.EditMessageRequest,
  Chat.EditMessageResponse,
);
export const DELETE_MESSAGE = defineSpec(
  MsgID.DELETE_MESSAGE_REQ,
  MsgID.DELETE_MESSAGE_RESP,
  Chat.DeleteMessageRequest,
  Chat.DeleteMessageResponse,
);

// Social plane (WS 8001): the backend serves the full roster surface
// (friends, pending requests, blocked list); friends stay pending-request
// based and presence is a broadcast snapshot.
export const ADD_FRIEND = defineSpec(
  MsgID.ADD_FRIEND_REQ,
  MsgID.ADD_FRIEND_RESP,
  Social.AddFriendRequest,
  Social.AddFriendResponse,
);
export const FRIEND_REQUEST_ACTION = defineSpec(
  MsgID.FRIEND_REQUEST_ACTION_REQ,
  MsgID.FRIEND_REQUEST_ACTION_RESP,
  Social.FriendRequestAction,
  Social.FriendRequestActionResponse,
);
export const REMOVE_FRIEND = defineSpec(
  MsgID.REMOVE_FRIEND_REQ,
  MsgID.REMOVE_FRIEND_RESP,
  Social.RemoveFriendRequest,
  Social.RemoveFriendResponse,
);
export const GET_FRIEND_LIST = defineSpec(
  MsgID.GET_FRIEND_LIST_REQ,
  MsgID.GET_FRIEND_LIST_RESP,
  Social.GetFriendListRequest,
  Social.GetFriendListResponse,
);
export const GET_PENDING_REQUESTS = defineSpec(
  MsgID.GET_PENDING_REQUESTS_REQ,
  MsgID.GET_PENDING_REQUESTS_RESP,
  Social.GetPendingRequestsRequest,
  Social.GetPendingRequestsResponse,
);
export const BLOCK_USER = defineSpec(
  MsgID.BLOCK_USER_REQ,
  MsgID.BLOCK_USER_RESP,
  Social.BlockUserRequest,
  Social.BlockUserResponse,
);
export const UNBLOCK_USER = defineSpec(
  MsgID.UNBLOCK_USER_REQ,
  MsgID.UNBLOCK_USER_RESP,
  Social.UnblockUserRequest,
  Social.UnblockUserResponse,
);
export const GET_BLOCKED_LIST = defineSpec(
  MsgID.GET_BLOCKED_LIST_REQ,
  MsgID.GET_BLOCKED_LIST_RESP,
  Social.GetBlockedListRequest,
  Social.GetBlockedListResponse,
);
export const SET_PRESENCE = defineSpec(
  MsgID.SET_PRESENCE_REQ,
  MsgID.SET_PRESENCE_RESP,
  Social.SetPresenceRequest,
  Social.SetPresenceResponse,
);
export const GET_PRESENCE = defineSpec(
  MsgID.GET_PRESENCE_REQ,
  MsgID.GET_PRESENCE_RESP,
  Social.GetPresenceRequest,
  Social.GetPresenceResponse,
);

// Party plane (WS 7501): cross-game team-up. Invite-accept only, snapshot
// sync (PARTY_STATE_CHANGED carries the full PartyInfo to every member).
export const CREATE_PARTY = defineSpec(
  MsgID.CREATE_PARTY_REQ,
  MsgID.CREATE_PARTY_RESP,
  Party.CreatePartyRequest,
  Party.CreatePartyResponse,
);
export const DISBAND_PARTY = defineSpec(
  MsgID.DISBAND_PARTY_REQ,
  MsgID.DISBAND_PARTY_RESP,
  Party.DisbandPartyRequest,
  Party.DisbandPartyResponse,
);
export const INVITE_TO_PARTY = defineSpec(
  MsgID.INVITE_TO_PARTY_REQ,
  MsgID.INVITE_TO_PARTY_RESP,
  Party.InviteToPartyRequest,
  Party.InviteToPartyResponse,
);
export const ACCEPT_PARTY_INVITE = defineSpec(
  MsgID.ACCEPT_INVITE_REQ,
  MsgID.ACCEPT_INVITE_RESP,
  Party.AcceptInviteRequest,
  Party.AcceptInviteResponse,
);
export const DECLINE_PARTY_INVITE = defineSpec(
  MsgID.DECLINE_INVITE_REQ,
  MsgID.DECLINE_INVITE_RESP,
  Party.DeclineInviteRequest,
  Party.DeclineInviteResponse,
);
export const LEAVE_PARTY = defineSpec(
  MsgID.LEAVE_PARTY_REQ,
  MsgID.LEAVE_PARTY_RESP,
  Party.LeavePartyRequest,
  Party.LeavePartyResponse,
);
export const KICK_PARTY_MEMBER = defineSpec(
  MsgID.KICK_PARTY_MEMBER_REQ,
  MsgID.KICK_PARTY_MEMBER_RESP,
  Party.KickMemberRequest,
  Party.KickMemberResponse,
);
export const TRANSFER_PARTY_LEADER = defineSpec(
  MsgID.TRANSFER_LEADER_REQ,
  MsgID.TRANSFER_LEADER_RESP,
  Party.TransferLeaderRequest,
  Party.TransferLeaderResponse,
);
export const SET_PARTY_READY = defineSpec(
  MsgID.SET_READY_REQ,
  MsgID.SET_READY_RESP,
  Party.SetReadyRequest,
  Party.SetReadyResponse,
);
export const GET_MY_PARTY = defineSpec(
  MsgID.GET_MY_PARTY_REQ,
  MsgID.GET_MY_PARTY_RESP,
  Party.GetMyPartyRequest,
  Party.GetMyPartyResponse,
);
