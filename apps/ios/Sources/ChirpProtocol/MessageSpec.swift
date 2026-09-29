import ChirpProtos
import SwiftProtobuf

/// A typed request/response pair. On this protocol a response is correlated
/// by Packet.sequence, not by msgId — the server answers with the RESP msgId
/// and echoes the request's sequence. The spec keeps both ids and the decoder
/// in one place so call sites never touch raw msgIds.
/// Port of mobile_companion lib/protocol/msg_map.dart (all 40 pairs).
public struct MessageSpec<T: SwiftProtobuf.Message> {
    public let reqMsgId: Chirp_Gateway_MsgID
    public let respMsgId: Chirp_Gateway_MsgID
    public let decodeResponse: ([UInt8]) throws -> T

    public init(
        _ reqMsgId: Chirp_Gateway_MsgID,
        _ respMsgId: Chirp_Gateway_MsgID,
        _ decodeResponse: @escaping ([UInt8]) throws -> T
    ) {
        self.reqMsgId = reqMsgId
        self.respMsgId = respMsgId
        self.decodeResponse = decodeResponse
    }
}

/// The full request table. Core pair (login) used by the connection layer's
/// consumers; the connection itself only needs ids and raw bodies.
public enum MsgSpecs {
    public static let login = MessageSpec(
        .loginReq, .loginResp
    ) { try Chirp_Auth_LoginResponse(serializedBytes: $0) }
    public static let logout = MessageSpec(
        .logoutReq, .logoutResp
    ) { try Chirp_Auth_LogoutResponse(serializedBytes: $0) }

    // Chat (direct chat WS entry).
    public static let sendMessage = MessageSpec(
        .sendMessageReq, .sendMessageResp
    ) { try Chirp_Chat_SendMessageResponse(serializedBytes: $0) }
    public static let getHistory = MessageSpec(
        .getHistoryReq, .getHistoryResp
    ) { try Chirp_Chat_GetHistoryResponse(serializedBytes: $0) }
    public static let markRead = MessageSpec(
        .markReadReq, .markReadResp
    ) { try Chirp_Chat_MarkReadResponse(serializedBytes: $0) }
    public static let getUserGroups = MessageSpec(
        .getUserGroupsReq, .getUserGroupsResp
    ) { try Chirp_Chat_GetUserGroupsResponse(serializedBytes: $0) }
    public static let createGroup = MessageSpec(
        .createGroupReq, .createGroupResp
    ) { try Chirp_Chat_CreateGroupResponse(serializedBytes: $0) }
    public static let inviteToGroup = MessageSpec(
        .inviteToGroupReq, .inviteToGroupResp
    ) { try Chirp_Chat_InviteToGroupResponse(serializedBytes: $0) }
    public static let leaveGroup = MessageSpec(
        .leaveGroupReq, .leaveGroupResp
    ) { try Chirp_Chat_LeaveGroupResponse(serializedBytes: $0) }
    public static let kickMember = MessageSpec(
        .kickMemberReq, .kickMemberResp
    ) { try Chirp_Chat_KickMemberResponse(serializedBytes: $0) }
    public static let getGroupMembers = MessageSpec(
        .getGroupMembersReq, .getGroupMembersResp
    ) { try Chirp_Chat_GetGroupMembersResponse(serializedBytes: $0) }

    // Reactions, edit/delete (typing has no REQ: it is fire-and-forget 2208).
    public static let addReaction = MessageSpec(
        .addReactionReq, .addReactionResp
    ) { try Chirp_Chat_AddReactionResponse(serializedBytes: $0) }
    public static let removeReaction = MessageSpec(
        .removeReactionReq, .removeReactionResp
    ) { try Chirp_Chat_RemoveReactionResponse(serializedBytes: $0) }
    public static let editMessage = MessageSpec(
        .editMessageReq, .editMessageResp
    ) { try Chirp_Chat_EditMessageResponse(serializedBytes: $0) }
    public static let deleteMessage = MessageSpec(
        .deleteMessageReq, .deleteMessageResp
    ) { try Chirp_Chat_DeleteMessageResponse(serializedBytes: $0) }

    // Social plane (WS 8001): friends and presence.
    public static let addFriend = MessageSpec(
        .addFriendReq, .addFriendResp
    ) { try Chirp_Social_AddFriendResponse(serializedBytes: $0) }
    public static let friendRequestAction = MessageSpec(
        .friendRequestActionReq, .friendRequestActionResp
    ) { try Chirp_Social_FriendRequestActionResponse(serializedBytes: $0) }
    public static let removeFriend = MessageSpec(
        .removeFriendReq, .removeFriendResp
    ) { try Chirp_Social_RemoveFriendResponse(serializedBytes: $0) }
    public static let getFriendList = MessageSpec(
        .getFriendListReq, .getFriendListResp
    ) { try Chirp_Social_GetFriendListResponse(serializedBytes: $0) }
    public static let getPendingRequests = MessageSpec(
        .getPendingRequestsReq, .getPendingRequestsResp
    ) { try Chirp_Social_GetPendingRequestsResponse(serializedBytes: $0) }
    public static let blockUser = MessageSpec(
        .blockUserReq, .blockUserResp
    ) { try Chirp_Social_BlockUserResponse(serializedBytes: $0) }
    public static let unblockUser = MessageSpec(
        .unblockUserReq, .unblockUserResp
    ) { try Chirp_Social_UnblockUserResponse(serializedBytes: $0) }
    public static let getBlockedList = MessageSpec(
        .getBlockedListReq, .getBlockedListResp
    ) { try Chirp_Social_GetBlockedListResponse(serializedBytes: $0) }
    public static let setPresence = MessageSpec(
        .setPresenceReq, .setPresenceResp
    ) { try Chirp_Social_SetPresenceResponse(serializedBytes: $0) }
    public static let getPresence = MessageSpec(
        .getPresenceReq, .getPresenceResp
    ) { try Chirp_Social_GetPresenceResponse(serializedBytes: $0) }

    // Party plane (WS 7501): cross-game team-up. Invite-accept only, snapshot
    // sync (PARTY_STATE_CHANGED carries the full PartyInfo to every member).
    public static let createParty = MessageSpec(
        .createPartyReq, .createPartyResp
    ) { try Chirp_Party_CreatePartyResponse(serializedBytes: $0) }
    public static let disbandParty = MessageSpec(
        .disbandPartyReq, .disbandPartyResp
    ) { try Chirp_Party_DisbandPartyResponse(serializedBytes: $0) }
    public static let inviteToParty = MessageSpec(
        .inviteToPartyReq, .inviteToPartyResp
    ) { try Chirp_Party_InviteToPartyResponse(serializedBytes: $0) }
    public static let acceptPartyInvite = MessageSpec(
        .acceptInviteReq, .acceptInviteResp
    ) { try Chirp_Party_AcceptInviteResponse(serializedBytes: $0) }
    public static let declinePartyInvite = MessageSpec(
        .declineInviteReq, .declineInviteResp
    ) { try Chirp_Party_DeclineInviteResponse(serializedBytes: $0) }
    public static let leaveParty = MessageSpec(
        .leavePartyReq, .leavePartyResp
    ) { try Chirp_Party_LeavePartyResponse(serializedBytes: $0) }
    public static let kickPartyMember = MessageSpec(
        .kickPartyMemberReq, .kickPartyMemberResp
    ) { try Chirp_Party_KickMemberResponse(serializedBytes: $0) }
    public static let transferPartyLeader = MessageSpec(
        .transferLeaderReq, .transferLeaderResp
    ) { try Chirp_Party_TransferLeaderResponse(serializedBytes: $0) }
    public static let setPartyReady = MessageSpec(
        .setReadyReq, .setReadyResp
    ) { try Chirp_Party_SetReadyResponse(serializedBytes: $0) }
    public static let getMyParty = MessageSpec(
        .getMyPartyReq, .getMyPartyResp
    ) { try Chirp_Party_GetMyPartyResponse(serializedBytes: $0) }

    // Device plane (app_gateway WS 5201): registration / listing of the push
    // targets for our account. app_gateway authenticates the session and pins
    // user_id server-side; 6009 PUSH_NOTIFICATION is deliberately absent —
    // the edge refuses it from clients.
    public static let registerDevice = MessageSpec(
        .registerDeviceReq, .registerDeviceResp
    ) { try Chirp_AppNotification_RegisterDeviceResponse(serializedBytes: $0) }
    public static let unregisterDevice = MessageSpec(
        .unregisterDeviceReq, .unregisterDeviceResp
    ) { try Chirp_AppNotification_UnregisterDeviceResponse(serializedBytes: $0) }
    public static let getUserDevices = MessageSpec(
        .getUserDevicesReq, .getUserDevicesResp
    ) { try Chirp_AppNotification_GetUserDevicesResponse(serializedBytes: $0) }

    // 游戏在线状态（app_gateway self-service）: the per-player switch for the
    // friend-facing "in game X" status and the friend-DM relay into the game
    // plane. Binding opts in by default (the switch reads true when never
    // set); the server pins player_id.
    public static let setGamePresenceEnabled = MessageSpec(
        .setGamePresenceEnabledReq, .setGamePresenceEnabledResp
    ) { try Chirp_GameServerGateway_SetGamePresenceEnabledResponse(serializedBytes: $0) }
    public static let getGamePresence = MessageSpec(
        .getGamePresenceReq, .getGamePresenceResp
    ) { try Chirp_GameServerGateway_GetGamePresenceResponse(serializedBytes: $0) }

    /// Every spec, for whole-table integrity assertions.
    public static let all: [AnyMessageSpec] = [
        AnyMessageSpec(login), AnyMessageSpec(logout),
        AnyMessageSpec(sendMessage), AnyMessageSpec(getHistory), AnyMessageSpec(markRead),
        AnyMessageSpec(getUserGroups), AnyMessageSpec(createGroup), AnyMessageSpec(inviteToGroup),
        AnyMessageSpec(leaveGroup), AnyMessageSpec(kickMember), AnyMessageSpec(getGroupMembers),
        AnyMessageSpec(addReaction), AnyMessageSpec(removeReaction), AnyMessageSpec(editMessage),
        AnyMessageSpec(deleteMessage),
        AnyMessageSpec(addFriend), AnyMessageSpec(friendRequestAction),
        AnyMessageSpec(removeFriend), AnyMessageSpec(getFriendList),
        AnyMessageSpec(getPendingRequests),
        AnyMessageSpec(blockUser), AnyMessageSpec(unblockUser), AnyMessageSpec(getBlockedList),
        AnyMessageSpec(setPresence), AnyMessageSpec(getPresence),
        AnyMessageSpec(createParty), AnyMessageSpec(disbandParty), AnyMessageSpec(inviteToParty),
        AnyMessageSpec(acceptPartyInvite), AnyMessageSpec(declinePartyInvite),
        AnyMessageSpec(leaveParty), AnyMessageSpec(kickPartyMember),
        AnyMessageSpec(transferPartyLeader), AnyMessageSpec(setPartyReady),
        AnyMessageSpec(getMyParty),
        AnyMessageSpec(registerDevice), AnyMessageSpec(unregisterDevice),
        AnyMessageSpec(getUserDevices),
        AnyMessageSpec(setGamePresenceEnabled), AnyMessageSpec(getGamePresence),
    ]
}

/// Type-erased view over a spec, so the whole-table assertions don't need
/// generics gymnastics.
public struct AnyMessageSpec {
    public let reqMsgId: Chirp_Gateway_MsgID
    public let respMsgId: Chirp_Gateway_MsgID

    /// The erased decoder — still per-spec (each closure knows its type);
    /// must not throw on an empty proto3 payload.
    public let decodeEmpty: () throws -> Void

    init<T: SwiftProtobuf.Message>(_ spec: MessageSpec<T>) {
        self.reqMsgId = spec.reqMsgId
        self.respMsgId = spec.respMsgId
        self.decodeEmpty = { _ = try spec.decodeResponse([]) }
    }
}
