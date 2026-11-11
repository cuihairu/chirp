using System;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>
/// Voice surface tests (VOICE-RT SDK 语音面): the typed voice specs, the
/// mic-queue request bodies on the wire, the join tier fields, and the
/// voice notify subscriptions — all against the scripted FakeTransport,
/// mirroring the voice service's wire behavior pinned by voice_tests.
/// </summary>
public class ChirpVoiceClientTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    private static void Respond(FakeTransport transport, Packet request, MsgID respMsgId,
        IMessage response)
    {
        transport.ServerPacket(new Packet
        {
            MsgId = respMsgId,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(response.ToByteArray()),
        });
    }

    [Fact]
    public async Task JoinVoiceRoom_CarriesTierFieldsThroughTheSpec()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.JoinVoiceRoomAsync("u1", "room_v1", "");
        await Settle();
        var sent = transport.LastSentPacket();
        Assert.Equal(MsgID.JoinRoomReq, sent.MsgId);
        Assert.Equal(new Chirp.Voice.JoinRoomRequest { UserId = "u1", RoomId = "room_v1" }.ToByteArray(),
            sent.Body.ToByteArray());

        Respond(transport, sent, MsgID.JoinRoomResp, new Chirp.Voice.JoinRoomResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            RoomId = "room_v1",
            MediaTier = Chirp.Voice.MediaTier.ForwardBypass,
            RtcpFeedbackMs = 2000,
        });

        var joined = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, joined.Code);
        // The tier lands on the typed response: the engine reads it to pick
        // full mixing vs the SFU bypass (VOICE-RT 旁路分级).
        Assert.Equal(Chirp.Voice.MediaTier.ForwardBypass, joined.MediaTier);
        Assert.Equal(2000, joined.RtcpFeedbackMs);
    }

    [Fact]
    public async Task MicQueueFlow_TypedBodies_AndResponses()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        // Host switches to managed mode.
        var modePending = client.SetVoiceMicModeAsync("host", "room_v1", Chirp.Voice.MicMode.MicManaged);
        await Settle();
        var modeSent = transport.LastSentPacket();
        Assert.Equal(MsgID.SetMicModeReq, modeSent.MsgId);
        Assert.Equal(
            new Chirp.Voice.SetMicModeRequest
            {
                UserId = "host",
                RoomId = "room_v1",
                Mode = Chirp.Voice.MicMode.MicManaged,
            }.ToByteArray(),
            modeSent.Body.ToByteArray());
        Respond(transport, modeSent, MsgID.SetMicModeResp, new Chirp.Voice.MicQueueResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            HolderUserId = "",
            QueueDepth = 0,
        });
        var modeResp = await modePending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, modeResp.Code);
        Assert.Equal(0, modeResp.QueueDepth);

        // A member applies.
        var applyPending = client.ApplyVoiceMicAsync("alice", "room_v1");
        await Settle();
        var applySent = transport.LastSentPacket();
        Assert.Equal(MsgID.MicQueueApplyReq, applySent.MsgId);
        Assert.Equal(
            new Chirp.Voice.MicQueueApplyRequest { UserId = "alice", RoomId = "room_v1" }.ToByteArray(),
            applySent.Body.ToByteArray());
        Respond(transport, applySent, MsgID.MicQueueApplyResp, new Chirp.Voice.MicQueueResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            QueueDepth = 1,
        });
        Assert.Equal(1, (await applyPending).QueueDepth);

        // The host approves the queue head (null target).
        var approvePending = client.ApproveVoiceMicAsync("host", "room_v1", null);
        await Settle();
        var approveSent = transport.LastSentPacket();
        Assert.Equal(MsgID.MicQueueApproveReq, approveSent.MsgId);
        Assert.Equal(
            new Chirp.Voice.MicQueueApproveRequest
            {
                UserId = "host",
                RoomId = "room_v1",
                TargetUserId = "",
            }.ToByteArray(),
            approveSent.Body.ToByteArray());
        Respond(transport, approveSent, MsgID.MicQueueApproveResp, new Chirp.Voice.MicQueueResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            HolderUserId = "alice",
        });
        var approveResp = await approvePending;
        Assert.Equal("alice", approveResp.HolderUserId);

        // 切麦: grant bypasses the queue.
        var grantPending = client.GrantVoiceMicAsync("host", "room_v1", "bob");
        await Settle();
        var grantSent = transport.LastSentPacket();
        Assert.Equal(MsgID.MicQueueGrantReq, grantSent.MsgId);
        Assert.Equal(
            new Chirp.Voice.MicQueueGrantRequest
            {
                UserId = "host",
                RoomId = "room_v1",
                TargetUserId = "bob",
            }.ToByteArray(),
            grantSent.Body.ToByteArray());
        Respond(transport, grantSent, MsgID.MicQueueGrantResp, new Chirp.Voice.MicQueueResponse
        {
            Code = Chirp.Common.ErrorCode.AuthFailed,  // non-host on the wire
        });
        Assert.Equal(Chirp.Common.ErrorCode.AuthFailed, (await grantPending).Code);

        // The holder steps down.
        var releasePending = client.ReleaseVoiceMicAsync("bob", "room_v1");
        await Settle();
        var releaseSent = transport.LastSentPacket();
        Assert.Equal(MsgID.MicQueueReleaseReq, releaseSent.MsgId);
        Respond(transport, releaseSent, MsgID.MicQueueReleaseResp, new Chirp.Voice.MicQueueResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
        });
        Assert.Equal(Chirp.Common.ErrorCode.Ok, (await releasePending).Code);
    }

    [Fact]
    public async Task VoiceNotifies_DecodeIntoTypedCallbacks_AndUnsubscribe()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        Chirp.Voice.SpeakingNotify? speaking = null;
        var speakingFired = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var unsubSpeaking = client.OnVoiceSpeaking(n =>
        {
            speaking = n;
            speakingFired.TrySetResult(true);
        });

        Chirp.Voice.MicQueueStateNotify? queueState = null;
        var queueFired = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        client.OnVoiceMicQueueState(n =>
        {
            queueState = n;
            queueFired.TrySetResult(true);
        });

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SpeakingNotify,
            Sequence = 0,
            Body = ByteString.CopyFrom(new Chirp.Voice.SpeakingNotify
            {
                RoomId = "room_v1",
                UserId = "alice",
                Speaking = true,
                Volume = 73,
            }.ToByteArray()),
        });
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.MicQueueStateNotify,
            Sequence = 0,
            Body = ByteString.CopyFrom(new Chirp.Voice.MicQueueStateNotify
            {
                RoomId = "room_v1",
                Mode = Chirp.Voice.MicMode.MicManaged,
                HolderUserId = "alice",
            }.ToByteArray()),
        });
        await Settle();
        Assert.True(await speakingFired.Task);
        Assert.True(await queueFired.Task);
        Assert.True(speaking!.Speaking);
        Assert.Equal(73, speaking.Volume);
        Assert.Equal(Chirp.Voice.MicMode.MicManaged, queueState!.Mode);
        Assert.Equal("alice", queueState.HolderUserId);

        // Unsubscribe silences the handler.
        unsubSpeaking();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SpeakingNotify,
            Sequence = 0,
            Body = ByteString.CopyFrom(new Chirp.Voice.SpeakingNotify
            {
                RoomId = "room_v1",
                UserId = "alice",
                Volume = 1,
            }.ToByteArray()),
        });
        await Settle();
        Assert.Equal(73, speaking.Volume);  // unchanged
    }

    [Fact]
    public async Task VoiceMuteAndRoomInfo_TypedRoundTrips()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var mutePending = client.SetVoiceMuteAsync("u1", "room_v1", true);
        await Settle();
        var muteSent = transport.LastSentPacket();
        Assert.Equal(MsgID.SetMuteReq, muteSent.MsgId);
        Assert.Equal(
            new Chirp.Voice.SetMuteRequest { UserId = "u1", RoomId = "room_v1", Muted = true }.ToByteArray(),
            muteSent.Body.ToByteArray());
        Respond(transport, muteSent, MsgID.SetMuteResp, new Chirp.Voice.SetMuteResponse { Code = Chirp.Common.ErrorCode.Ok });
        Assert.Equal(Chirp.Common.ErrorCode.Ok, (await mutePending).Code);

        var infoPending = client.GetVoiceRoomInfoAsync("room_v1");
        await Settle();
        var infoSent = transport.LastSentPacket();
        Assert.Equal(MsgID.GetRoomInfoReq, infoSent.MsgId);
        Respond(transport, infoSent, MsgID.GetRoomInfoResp, new Chirp.Voice.GetRoomInfoResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            RoomId = "room_v1",
            MicMode = Chirp.Voice.MicMode.MicManaged,
            MicHolderUserId = "alice",
        });
        var info = await infoPending;
        info.MicQueueUserIds.Add("bob");
        // (queue members arrive as a repeated field on the wire; here the
        // snapshot decode is what matters)
        Assert.Equal(Chirp.Voice.MicMode.MicManaged, info.MicMode);
        Assert.Equal("alice", info.MicHolderUserId);
    }
}
