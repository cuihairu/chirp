package chirp.mobile.protocol

import chirp.app_notification.AppNotification
import chirp.auth.Auth
import chirp.chat.Chat
import chirp.game_server_gateway.GameServerGateway
import chirp.gateway.Gateway
import chirp.party.Party
import chirp.social.Social
import com.google.protobuf.MessageLite

/**
 * A typed request/response pair. On this protocol a response is correlated
 * by Packet.sequence, not by msgId — the server answers with the RESP msgId
 * and echoes the request's sequence. The spec keeps both ids and the decoder
 * in one place so call sites never touch raw msgIds.
 * Port of mobile_companion lib/protocol/msg_map.dart (all 40 pairs).
 */
class MessageSpec<T : MessageLite>(
    val reqMsgId: Gateway.MsgID,
    val respMsgId: Gateway.MsgID,
    val decodeResponse: (ByteArray) -> T,
)

// Core pair used by the connection layer itself.
object MsgSpecs {
    val login = MessageSpec(
        Gateway.MsgID.LOGIN_REQ, Gateway.MsgID.LOGIN_RESP, Auth.LoginResponse::parseFrom,
    )
    val logout = MessageSpec(
        Gateway.MsgID.LOGOUT_REQ, Gateway.MsgID.LOGOUT_RESP, Auth.LogoutResponse::parseFrom,
    )

    // Chat (direct chat WS entry).
    val sendMessage = MessageSpec(
        Gateway.MsgID.SEND_MESSAGE_REQ, Gateway.MsgID.SEND_MESSAGE_RESP, Chat.SendMessageResponse::parseFrom,
    )
    val getHistory = MessageSpec(
        Gateway.MsgID.GET_HISTORY_REQ, Gateway.MsgID.GET_HISTORY_RESP, Chat.GetHistoryResponse::parseFrom,
    )
    val markRead = MessageSpec(
        Gateway.MsgID.MARK_READ_REQ, Gateway.MsgID.MARK_READ_RESP, Chat.MarkReadResponse::parseFrom,
    )
    val getUserGroups = MessageSpec(
        Gateway.MsgID.GET_USER_GROUPS_REQ, Gateway.MsgID.GET_USER_GROUPS_RESP, Chat.GetUserGroupsResponse::parseFrom,
    )
    val createGroup = MessageSpec(
        Gateway.MsgID.CREATE_GROUP_REQ, Gateway.MsgID.CREATE_GROUP_RESP, Chat.CreateGroupResponse::parseFrom,
    )
    val inviteToGroup = MessageSpec(
        Gateway.MsgID.INVITE_TO_GROUP_REQ, Gateway.MsgID.INVITE_TO_GROUP_RESP, Chat.InviteToGroupResponse::parseFrom,
    )
    val leaveGroup = MessageSpec(
        Gateway.MsgID.LEAVE_GROUP_REQ, Gateway.MsgID.LEAVE_GROUP_RESP, Chat.LeaveGroupResponse::parseFrom,
    )
    val kickMember = MessageSpec(
        Gateway.MsgID.KICK_MEMBER_REQ, Gateway.MsgID.KICK_MEMBER_RESP, Chat.KickMemberResponse::parseFrom,
    )
    val getGroupMembers = MessageSpec(
        Gateway.MsgID.GET_GROUP_MEMBERS_REQ, Gateway.MsgID.GET_GROUP_MEMBERS_RESP, Chat.GetGroupMembersResponse::parseFrom,
    )

    // Reactions, edit/delete (typing has no REQ: it is fire-and-forget 2208).
    val addReaction = MessageSpec(
        Gateway.MsgID.ADD_REACTION_REQ, Gateway.MsgID.ADD_REACTION_RESP, Chat.AddReactionResponse::parseFrom,
    )
    val removeReaction = MessageSpec(
        Gateway.MsgID.REMOVE_REACTION_REQ, Gateway.MsgID.REMOVE_REACTION_RESP, Chat.RemoveReactionResponse::parseFrom,
    )
    val editMessage = MessageSpec(
        Gateway.MsgID.EDIT_MESSAGE_REQ, Gateway.MsgID.EDIT_MESSAGE_RESP, Chat.EditMessageResponse::parseFrom,
    )
    val deleteMessage = MessageSpec(
        Gateway.MsgID.DELETE_MESSAGE_REQ, Gateway.MsgID.DELETE_MESSAGE_RESP, Chat.DeleteMessageResponse::parseFrom,
    )

    // Social plane (WS 8001): friends and presence.
    val addFriend = MessageSpec(
        Gateway.MsgID.ADD_FRIEND_REQ, Gateway.MsgID.ADD_FRIEND_RESP, Social.AddFriendResponse::parseFrom,
    )
    val friendRequestAction = MessageSpec(
        Gateway.MsgID.FRIEND_REQUEST_ACTION_REQ, Gateway.MsgID.FRIEND_REQUEST_ACTION_RESP,
        Social.FriendRequestActionResponse::parseFrom,
    )
    val removeFriend = MessageSpec(
        Gateway.MsgID.REMOVE_FRIEND_REQ, Gateway.MsgID.REMOVE_FRIEND_RESP, Social.RemoveFriendResponse::parseFrom,
    )
    val getFriendList = MessageSpec(
        Gateway.MsgID.GET_FRIEND_LIST_REQ, Gateway.MsgID.GET_FRIEND_LIST_RESP, Social.GetFriendListResponse::parseFrom,
    )
    val getPendingRequests = MessageSpec(
        Gateway.MsgID.GET_PENDING_REQUESTS_REQ, Gateway.MsgID.GET_PENDING_REQUESTS_RESP,
        Social.GetPendingRequestsResponse::parseFrom,
    )
    val blockUser = MessageSpec(
        Gateway.MsgID.BLOCK_USER_REQ, Gateway.MsgID.BLOCK_USER_RESP, Social.BlockUserResponse::parseFrom,
    )
    val unblockUser = MessageSpec(
        Gateway.MsgID.UNBLOCK_USER_REQ, Gateway.MsgID.UNBLOCK_USER_RESP, Social.UnblockUserResponse::parseFrom,
    )
    val getBlockedList = MessageSpec(
        Gateway.MsgID.GET_BLOCKED_LIST_REQ, Gateway.MsgID.GET_BLOCKED_LIST_RESP, Social.GetBlockedListResponse::parseFrom,
    )
    val setPresence = MessageSpec(
        Gateway.MsgID.SET_PRESENCE_REQ, Gateway.MsgID.SET_PRESENCE_RESP, Social.SetPresenceResponse::parseFrom,
    )
    val getPresence = MessageSpec(
        Gateway.MsgID.GET_PRESENCE_REQ, Gateway.MsgID.GET_PRESENCE_RESP, Social.GetPresenceResponse::parseFrom,
    )

    // Party plane (WS 7501): cross-game team-up. Invite-accept only, snapshot
    // sync (PARTY_STATE_CHANGED carries the full PartyInfo to every member).
    val createParty = MessageSpec(
        Gateway.MsgID.CREATE_PARTY_REQ, Gateway.MsgID.CREATE_PARTY_RESP, Party.CreatePartyResponse::parseFrom,
    )
    val disbandParty = MessageSpec(
        Gateway.MsgID.DISBAND_PARTY_REQ, Gateway.MsgID.DISBAND_PARTY_RESP, Party.DisbandPartyResponse::parseFrom,
    )
    val inviteToParty = MessageSpec(
        Gateway.MsgID.INVITE_TO_PARTY_REQ, Gateway.MsgID.INVITE_TO_PARTY_RESP, Party.InviteToPartyResponse::parseFrom,
    )
    val acceptPartyInvite = MessageSpec(
        Gateway.MsgID.ACCEPT_INVITE_REQ, Gateway.MsgID.ACCEPT_INVITE_RESP, Party.AcceptInviteResponse::parseFrom,
    )
    val declinePartyInvite = MessageSpec(
        Gateway.MsgID.DECLINE_INVITE_REQ, Gateway.MsgID.DECLINE_INVITE_RESP, Party.DeclineInviteResponse::parseFrom,
    )
    val leaveParty = MessageSpec(
        Gateway.MsgID.LEAVE_PARTY_REQ, Gateway.MsgID.LEAVE_PARTY_RESP, Party.LeavePartyResponse::parseFrom,
    )
    val kickPartyMember = MessageSpec(
        Gateway.MsgID.KICK_PARTY_MEMBER_REQ, Gateway.MsgID.KICK_PARTY_MEMBER_RESP, Party.KickMemberResponse::parseFrom,
    )
    val transferPartyLeader = MessageSpec(
        Gateway.MsgID.TRANSFER_LEADER_REQ, Gateway.MsgID.TRANSFER_LEADER_RESP, Party.TransferLeaderResponse::parseFrom,
    )
    val setPartyReady = MessageSpec(
        Gateway.MsgID.SET_READY_REQ, Gateway.MsgID.SET_READY_RESP, Party.SetReadyResponse::parseFrom,
    )
    val getMyParty = MessageSpec(
        Gateway.MsgID.GET_MY_PARTY_REQ, Gateway.MsgID.GET_MY_PARTY_RESP, Party.GetMyPartyResponse::parseFrom,
    )

    // Device plane (app_gateway WS 5201): registration / listing of the push
    // targets for our account. app_gateway authenticates the session and pins
    // user_id server-side; 6009 PUSH_NOTIFICATION is deliberately absent —
    // the edge refuses it from clients.
    val registerDevice = MessageSpec(
        Gateway.MsgID.REGISTER_DEVICE_REQ, Gateway.MsgID.REGISTER_DEVICE_RESP,
        AppNotification.RegisterDeviceResponse::parseFrom,
    )
    val unregisterDevice = MessageSpec(
        Gateway.MsgID.UNREGISTER_DEVICE_REQ, Gateway.MsgID.UNREGISTER_DEVICE_RESP,
        AppNotification.UnregisterDeviceResponse::parseFrom,
    )
    val getUserDevices = MessageSpec(
        Gateway.MsgID.GET_USER_DEVICES_REQ, Gateway.MsgID.GET_USER_DEVICES_RESP,
        AppNotification.GetUserDevicesResponse::parseFrom,
    )

    // 游戏在线状态（app_gateway self-service）: the per-player switch for the
    // friend-facing "in game X" status and the friend-DM relay into the game
    // plane. Binding opts in by default (the switch reads true when never
    // set); the server pins player_id.
    val setGamePresenceEnabled = MessageSpec(
        Gateway.MsgID.SET_GAME_PRESENCE_ENABLED_REQ, Gateway.MsgID.SET_GAME_PRESENCE_ENABLED_RESP,
        GameServerGateway.SetGamePresenceEnabledResponse::parseFrom,
    )
    val getGamePresence = MessageSpec(
        Gateway.MsgID.GET_GAME_PRESENCE_REQ, Gateway.MsgID.GET_GAME_PRESENCE_RESP,
        GameServerGateway.GetGamePresenceResponse::parseFrom,
    )

    /** Every spec, for whole-table integrity assertions. */
    val all: List<MessageSpec<*>> = listOf(
        login, logout,
        sendMessage, getHistory, markRead, getUserGroups, createGroup, inviteToGroup,
        leaveGroup, kickMember, getGroupMembers,
        addReaction, removeReaction, editMessage, deleteMessage,
        addFriend, friendRequestAction, removeFriend, getFriendList, getPendingRequests,
        blockUser, unblockUser, getBlockedList, setPresence, getPresence,
        createParty, disbandParty, inviteToParty, acceptPartyInvite, declinePartyInvite,
        leaveParty, kickPartyMember, transferPartyLeader, setPartyReady, getMyParty,
        registerDevice, unregisterDevice, getUserDevices,
        setGamePresenceEnabled, getGamePresence,
    )
}
