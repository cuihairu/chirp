using Chirp.Gateway;

namespace Chirp.Sdk
{
    /// <summary>
    /// The typed request/response table, mirroring the web/mobile companions'
    /// msg_map. Dart needed prefixed imports to dodge name shadowing; C#
    /// namespaces make the bare type names unambiguous, so this is a flat
    /// static table.
    /// </summary>
    public static class Specs
    {
        // Core pair used by the connection layer itself.
        public static readonly MessageSpec<Chirp.Auth.LoginResponse> Login =
            new(MsgID.LoginReq, MsgID.LoginResp, Chirp.Auth.LoginResponse.Parser);
        public static readonly MessageSpec<Chirp.Auth.LogoutResponse> Logout =
            new(MsgID.LogoutReq, MsgID.LogoutResp, Chirp.Auth.LogoutResponse.Parser);

        // Chat (direct chat WS entry).
        public static readonly MessageSpec<Chirp.Chat.SendMessageResponse> SendMessage =
            new(MsgID.SendMessageReq, MsgID.SendMessageResp, Chirp.Chat.SendMessageResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.GetHistoryResponse> GetHistory =
            new(MsgID.GetHistoryReq, MsgID.GetHistoryResp, Chirp.Chat.GetHistoryResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.MarkReadResponse> MarkRead =
            new(MsgID.MarkReadReq, MsgID.MarkReadResp, Chirp.Chat.MarkReadResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.GetUserGroupsResponse> GetUserGroups =
            new(MsgID.GetUserGroupsReq, MsgID.GetUserGroupsResp, Chirp.Chat.GetUserGroupsResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.CreateGroupResponse> CreateGroup =
            new(MsgID.CreateGroupReq, MsgID.CreateGroupResp, Chirp.Chat.CreateGroupResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.InviteToGroupResponse> InviteToGroup =
            new(MsgID.InviteToGroupReq, MsgID.InviteToGroupResp, Chirp.Chat.InviteToGroupResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.LeaveGroupResponse> LeaveGroup =
            new(MsgID.LeaveGroupReq, MsgID.LeaveGroupResp, Chirp.Chat.LeaveGroupResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.KickMemberResponse> KickMember =
            new(MsgID.KickMemberReq, MsgID.KickMemberResp, Chirp.Chat.KickMemberResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.GetGroupMembersResponse> GetGroupMembers =
            new(MsgID.GetGroupMembersReq, MsgID.GetGroupMembersResp, Chirp.Chat.GetGroupMembersResponse.Parser);

        // Reactions, edit/delete (typing has no REQ: it is fire-and-forget).
        public static readonly MessageSpec<Chirp.Chat.AddReactionResponse> AddReaction =
            new(MsgID.AddReactionReq, MsgID.AddReactionResp, Chirp.Chat.AddReactionResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.RemoveReactionResponse> RemoveReaction =
            new(MsgID.RemoveReactionReq, MsgID.RemoveReactionResp, Chirp.Chat.RemoveReactionResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.EditMessageResponse> EditMessage =
            new(MsgID.EditMessageReq, MsgID.EditMessageResp, Chirp.Chat.EditMessageResponse.Parser);
        public static readonly MessageSpec<Chirp.Chat.DeleteMessageResponse> DeleteMessage =
            new(MsgID.DeleteMessageReq, MsgID.DeleteMessageResp, Chirp.Chat.DeleteMessageResponse.Parser);

        // Social plane (WS 8001): friends and presence.
        public static readonly MessageSpec<Chirp.Social.AddFriendResponse> AddFriend =
            new(MsgID.AddFriendReq, MsgID.AddFriendResp, Chirp.Social.AddFriendResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.FriendRequestActionResponse> FriendRequestAction =
            new(MsgID.FriendRequestActionReq, MsgID.FriendRequestActionResp, Chirp.Social.FriendRequestActionResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.RemoveFriendResponse> RemoveFriend =
            new(MsgID.RemoveFriendReq, MsgID.RemoveFriendResp, Chirp.Social.RemoveFriendResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.GetFriendListResponse> GetFriendList =
            new(MsgID.GetFriendListReq, MsgID.GetFriendListResp, Chirp.Social.GetFriendListResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.GetPendingRequestsResponse> GetPendingRequests =
            new(MsgID.GetPendingRequestsReq, MsgID.GetPendingRequestsResp, Chirp.Social.GetPendingRequestsResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.BlockUserResponse> BlockUser =
            new(MsgID.BlockUserReq, MsgID.BlockUserResp, Chirp.Social.BlockUserResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.UnblockUserResponse> UnblockUser =
            new(MsgID.UnblockUserReq, MsgID.UnblockUserResp, Chirp.Social.UnblockUserResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.GetBlockedListResponse> GetBlockedList =
            new(MsgID.GetBlockedListReq, MsgID.GetBlockedListResp, Chirp.Social.GetBlockedListResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.SetPresenceResponse> SetPresence =
            new(MsgID.SetPresenceReq, MsgID.SetPresenceResp, Chirp.Social.SetPresenceResponse.Parser);
        public static readonly MessageSpec<Chirp.Social.GetPresenceResponse> GetPresence =
            new(MsgID.GetPresenceReq, MsgID.GetPresenceResp, Chirp.Social.GetPresenceResponse.Parser);

        // Party plane (WS 7501): cross-game team-up. Invite-accept only,
        // snapshot sync (PARTY_STATE_CHANGED carries the full PartyInfo to
        // every member).
        public static readonly MessageSpec<Chirp.Party.CreatePartyResponse> CreateParty =
            new(MsgID.CreatePartyReq, MsgID.CreatePartyResp, Chirp.Party.CreatePartyResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.DisbandPartyResponse> DisbandParty =
            new(MsgID.DisbandPartyReq, MsgID.DisbandPartyResp, Chirp.Party.DisbandPartyResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.InviteToPartyResponse> InviteToParty =
            new(MsgID.InviteToPartyReq, MsgID.InviteToPartyResp, Chirp.Party.InviteToPartyResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.AcceptInviteResponse> AcceptPartyInvite =
            new(MsgID.AcceptInviteReq, MsgID.AcceptInviteResp, Chirp.Party.AcceptInviteResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.DeclineInviteResponse> DeclinePartyInvite =
            new(MsgID.DeclineInviteReq, MsgID.DeclineInviteResp, Chirp.Party.DeclineInviteResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.LeavePartyResponse> LeaveParty =
            new(MsgID.LeavePartyReq, MsgID.LeavePartyResp, Chirp.Party.LeavePartyResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.KickMemberResponse> KickPartyMember =
            new(MsgID.KickPartyMemberReq, MsgID.KickPartyMemberResp, Chirp.Party.KickMemberResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.TransferLeaderResponse> TransferPartyLeader =
            new(MsgID.TransferLeaderReq, MsgID.TransferLeaderResp, Chirp.Party.TransferLeaderResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.SetReadyResponse> SetPartyReady =
            new(MsgID.SetReadyReq, MsgID.SetReadyResp, Chirp.Party.SetReadyResponse.Parser);
        public static readonly MessageSpec<Chirp.Party.GetMyPartyResponse> GetMyParty =
            new(MsgID.GetMyPartyReq, MsgID.GetMyPartyResp, Chirp.Party.GetMyPartyResponse.Parser);

        // Device plane (app_gateway WS 5201): registration / listing of the
        // push targets for our account. app_gateway authenticates the session
        // and pins user_id server-side; PUSH_NOTIFICATION is deliberately
        // absent — the edge refuses it from clients.
        public static readonly MessageSpec<Chirp.AppNotification.RegisterDeviceResponse> RegisterDevice =
            new(MsgID.RegisterDeviceReq, MsgID.RegisterDeviceResp, Chirp.AppNotification.RegisterDeviceResponse.Parser);
        public static readonly MessageSpec<Chirp.AppNotification.UnregisterDeviceResponse> UnregisterDevice =
            new(MsgID.UnregisterDeviceReq, MsgID.UnregisterDeviceResp, Chirp.AppNotification.UnregisterDeviceResponse.Parser);
        public static readonly MessageSpec<Chirp.AppNotification.GetUserDevicesResponse> GetUserDevices =
            new(MsgID.GetUserDevicesReq, MsgID.GetUserDevicesResp, Chirp.AppNotification.GetUserDevicesResponse.Parser);

        // Voice plane (voice WS 9001): game voice-room signaling. Join carries
        // the WebRTC SDP offer and the response brings the answer plus TURN
        // short-term credentials (IceServer). ICE/SDP relay (4007-4009) and
        // the participant/speaking notifies are fire-and-forget or pushes —
        // no spec; subscribe with OnNotify / client.Send.
        public static readonly MessageSpec<Chirp.Voice.CreateRoomResponse> CreateVoiceRoom =
            new(MsgID.CreateRoomReq, MsgID.CreateRoomResp, Chirp.Voice.CreateRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.JoinRoomResponse> JoinVoiceRoom =
            new(MsgID.JoinRoomReq, MsgID.JoinRoomResp, Chirp.Voice.JoinRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.LeaveRoomResponse> LeaveVoiceRoom =
            new(MsgID.LeaveRoomReq, MsgID.LeaveRoomResp, Chirp.Voice.LeaveRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.GetRoomInfoResponse> GetVoiceRoomInfo =
            new(MsgID.GetRoomInfoReq, MsgID.GetRoomInfoResp, Chirp.Voice.GetRoomInfoResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.GetUserRoomResponse> GetUserVoiceRoom =
            new(MsgID.GetUserRoomReq, MsgID.GetUserRoomResp, Chirp.Voice.GetUserRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.SetMuteResponse> SetVoiceMute =
            new(MsgID.SetMuteReq, MsgID.SetMuteResp, Chirp.Voice.SetMuteResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.SetDeafenResponse> SetVoiceDeafen =
            new(MsgID.SetDeafenReq, MsgID.SetDeafenResp, Chirp.Voice.SetDeafenResponse.Parser);
    }
}
