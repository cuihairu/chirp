using System;
using System.Threading.Tasks;
using Chirp.Gateway;

namespace Chirp.Sdk
{
    /// <summary>
    /// Typed specs for the voice plane (VOICE-RT). One static table keeps
    /// the request/response msgId pairs and decoders off the call sites,
    /// mirroring the chat specs.
    /// </summary>
    public static class ChirpVoiceSpecs
    {
        public static readonly MessageSpec<Chirp.Voice.CreateRoomResponse> CreateRoom =
            new MessageSpec<Chirp.Voice.CreateRoomResponse>(MsgID.CreateRoomReq, MsgID.CreateRoomResp,
                Chirp.Voice.CreateRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.JoinRoomResponse> JoinRoom =
            new MessageSpec<Chirp.Voice.JoinRoomResponse>(MsgID.JoinRoomReq, MsgID.JoinRoomResp,
                Chirp.Voice.JoinRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.LeaveRoomResponse> LeaveRoom =
            new MessageSpec<Chirp.Voice.LeaveRoomResponse>(MsgID.LeaveRoomReq, MsgID.LeaveRoomResp,
                Chirp.Voice.LeaveRoomResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.GetRoomInfoResponse> GetRoomInfo =
            new MessageSpec<Chirp.Voice.GetRoomInfoResponse>(MsgID.GetRoomInfoReq, MsgID.GetRoomInfoResp,
                Chirp.Voice.GetRoomInfoResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.SetMuteResponse> SetMute =
            new MessageSpec<Chirp.Voice.SetMuteResponse>(MsgID.SetMuteReq, MsgID.SetMuteResp,
                Chirp.Voice.SetMuteResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.SetDeafenResponse> SetDeafen =
            new MessageSpec<Chirp.Voice.SetDeafenResponse>(MsgID.SetDeafenReq, MsgID.SetDeafenResp,
                Chirp.Voice.SetDeafenResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.MicQueueResponse> SetMicMode =
            new MessageSpec<Chirp.Voice.MicQueueResponse>(MsgID.SetMicModeReq, MsgID.SetMicModeResp,
                Chirp.Voice.MicQueueResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.MicQueueResponse> MicQueueApply =
            new MessageSpec<Chirp.Voice.MicQueueResponse>(MsgID.MicQueueApplyReq, MsgID.MicQueueApplyResp,
                Chirp.Voice.MicQueueResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.MicQueueResponse> MicQueueApprove =
            new MessageSpec<Chirp.Voice.MicQueueResponse>(MsgID.MicQueueApproveReq, MsgID.MicQueueApproveResp,
                Chirp.Voice.MicQueueResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.MicQueueResponse> MicQueueRelease =
            new MessageSpec<Chirp.Voice.MicQueueResponse>(MsgID.MicQueueReleaseReq, MsgID.MicQueueReleaseResp,
                Chirp.Voice.MicQueueResponse.Parser);
        public static readonly MessageSpec<Chirp.Voice.MicQueueResponse> MicQueueGrant =
            new MessageSpec<Chirp.Voice.MicQueueResponse>(MsgID.MicQueueGrantReq, MsgID.MicQueueGrantResp,
                Chirp.Voice.MicQueueResponse.Parser);
    }

    /// <summary>
    /// Voice surface on the shared client (VOICE-RT SDK 语音面): the voice
    /// service speaks the same Packet protocol and LOGIN handshake as the
    /// chat gateway, so one connection carries both — point the client's
    /// websocket at the voice ws_port (or an aggregator edge) and use these
    /// methods. Transport errors surface as RequestError(Closed/Timeout);
    /// business results read resp.Code (room missing, non-host, …).
    /// </summary>
    public sealed partial class ChirpClient
    {
        /// <summary>JoinRoomResponse carries the server's delivery tier
        /// (VOICE-RT 旁路分级): FullMix/ForwardBypass plus the tier's RTCP
        /// receiver-report pacing in ms. The engine configures mixing or the
        /// SFU bypass from it.</summary>
        public Task<Chirp.Voice.JoinRoomResponse> JoinVoiceRoomAsync(string userId, string roomId,
            string sdpOffer, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.JoinRoomRequest { UserId = userId, RoomId = roomId, SdpOffer = sdpOffer };
            return RequestAsync(ChirpVoiceSpecs.JoinRoom, req, timeoutMs);
        }

        public Task<Chirp.Voice.CreateRoomResponse> CreateVoiceRoomAsync(string userId,
            Chirp.Voice.RoomType roomType, string roomName, int maxParticipants = 0,
            int? timeoutMs = null)
        {
            var req = new Chirp.Voice.CreateRoomRequest
            {
                UserId = userId,
                RoomType = roomType,
                RoomName = roomName,
                MaxParticipants = maxParticipants,
            };
            return RequestAsync(ChirpVoiceSpecs.CreateRoom, req, timeoutMs);
        }

        public Task<Chirp.Voice.LeaveRoomResponse> LeaveVoiceRoomAsync(string userId, string roomId,
            int? timeoutMs = null)
        {
            var req = new Chirp.Voice.LeaveRoomRequest { UserId = userId, RoomId = roomId };
            return RequestAsync(ChirpVoiceSpecs.LeaveRoom, req, timeoutMs);
        }

        /// <summary>Room snapshot including the speaking-queue state
        /// (mic mode, holder, FIFO) for late joiners.</summary>
        public Task<Chirp.Voice.GetRoomInfoResponse> GetVoiceRoomInfoAsync(string roomId,
            int? timeoutMs = null)
        {
            var req = new Chirp.Voice.GetRoomInfoRequest { RoomId = roomId };
            return RequestAsync(ChirpVoiceSpecs.GetRoomInfo, req, timeoutMs);
        }

        public Task<Chirp.Voice.SetMuteResponse> SetVoiceMuteAsync(string userId, string roomId,
            bool muted, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.SetMuteRequest { UserId = userId, RoomId = roomId, Muted = muted };
            return RequestAsync(ChirpVoiceSpecs.SetMute, req, timeoutMs);
        }

        public Task<Chirp.Voice.SetDeafenResponse> SetVoiceDeafenAsync(string userId, string roomId,
            bool deafened, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.SetDeafenRequest { UserId = userId, RoomId = roomId, Deafened = deafened };
            return RequestAsync(ChirpVoiceSpecs.SetDeafen, req, timeoutMs);
        }

        // ----- speaking queue (麦序模式): host-only ops answer AuthFailed
        // for members; every mutation fans a MicQueueStateNotify -----

        public Task<Chirp.Voice.MicQueueResponse> SetVoiceMicModeAsync(string userId, string roomId,
            Chirp.Voice.MicMode mode, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.SetMicModeRequest { UserId = userId, RoomId = roomId, Mode = mode };
            return RequestAsync(ChirpVoiceSpecs.SetMicMode, req, timeoutMs);
        }

        public Task<Chirp.Voice.MicQueueResponse> ApplyVoiceMicAsync(string userId, string roomId,
            int? timeoutMs = null)
        {
            var req = new Chirp.Voice.MicQueueApplyRequest { UserId = userId, RoomId = roomId };
            return RequestAsync(ChirpVoiceSpecs.MicQueueApply, req, timeoutMs);
        }

        /// <summary>targetUserId null/empty = approve the queue head.</summary>
        public Task<Chirp.Voice.MicQueueResponse> ApproveVoiceMicAsync(string userId, string roomId,
            string? targetUserId, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.MicQueueApproveRequest
            {
                UserId = userId,
                RoomId = roomId,
                TargetUserId = targetUserId ?? "",
            };
            return RequestAsync(ChirpVoiceSpecs.MicQueueApprove, req, timeoutMs);
        }

        public Task<Chirp.Voice.MicQueueResponse> ReleaseVoiceMicAsync(string userId, string roomId,
            int? timeoutMs = null)
        {
            var req = new Chirp.Voice.MicQueueReleaseRequest { UserId = userId, RoomId = roomId };
            return RequestAsync(ChirpVoiceSpecs.MicQueueRelease, req, timeoutMs);
        }

        /// <summary>Host hands the mic straight to a member, bypassing the
        /// queue (切麦).</summary>
        public Task<Chirp.Voice.MicQueueResponse> GrantVoiceMicAsync(string userId, string roomId,
            string targetUserId, int? timeoutMs = null)
        {
            var req = new Chirp.Voice.MicQueueGrantRequest
            {
                UserId = userId,
                RoomId = roomId,
                TargetUserId = targetUserId,
            };
            return RequestAsync(ChirpVoiceSpecs.MicQueueGrant, req, timeoutMs);
        }

        // ----- typed notify subscriptions. Each returns the unsubscribe
        // action from OnNotify; handlers run on the transport thread. -----

        /// <summary>Speaking state with the 0-100 loudness level (0 when
        /// speaking is false) that drives volume UI.</summary>
        public Action OnVoiceSpeaking(Action<Chirp.Voice.SpeakingNotify> handler) =>
            OnNotify(MsgID.SpeakingNotify, body => handler(Chirp.Voice.SpeakingNotify.Parser.ParseFrom(body)));

        public Action OnVoiceParticipantJoined(Action<Chirp.Voice.ParticipantJoinedNotify> handler) =>
            OnNotify(MsgID.ParticipantJoinedNotify,
                body => handler(Chirp.Voice.ParticipantJoinedNotify.Parser.ParseFrom(body)));

        public Action OnVoiceParticipantLeft(Action<Chirp.Voice.ParticipantLeftNotify> handler) =>
            OnNotify(MsgID.ParticipantLeftNotify,
                body => handler(Chirp.Voice.ParticipantLeftNotify.Parser.ParseFrom(body)));

        public Action OnVoiceParticipantStateChanged(Action<Chirp.Voice.ParticipantStateChangedNotify> handler) =>
            OnNotify(MsgID.ParticipantStateChangedNotify,
                body => handler(Chirp.Voice.ParticipantStateChangedNotify.Parser.ParseFrom(body)));

        public Action OnVoiceMicQueueState(Action<Chirp.Voice.MicQueueStateNotify> handler) =>
            OnNotify(MsgID.MicQueueStateNotify,
                body => handler(Chirp.Voice.MicQueueStateNotify.Parser.ParseFrom(body)));
    }
}
