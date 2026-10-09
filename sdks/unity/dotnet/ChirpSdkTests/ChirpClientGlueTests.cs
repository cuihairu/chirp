using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>Scriptable transport for the client's network-glue cases —
/// the seams FakeTransport can't script: a dispose hook (reaches the stale
/// teardown window of ConnectAsync), a faulting SendAsync (a socket dying
/// under the heartbeat), and a gated open (a kick racing an in-flight
/// connect).</summary>
internal sealed class GlueTransport : IChirpTransport
{
    public event Action<byte[]>? BinaryMessage;
    public event Action? Closed;

    public readonly List<byte[]> Sent = new();
    private readonly object _gate = new();

    public int CloseCount;
    public int SendAttempts;

    /// <summary>Invoked from Dispose() — ConnectAsync disposes the stale
    /// transport while Status still reads Connected.</summary>
    public Action? OnDispose;

    /// <summary>When set, SendAsync records the attempt and faults with it.</summary>
    public Exception? SendError;

    /// <summary>When set, OpenAsync awaits it instead of completing.</summary>
    public Task? OpenGate;

    public Task OpenAsync(string url, CancellationToken ct) => OpenGate ?? Task.CompletedTask;

    public Task SendAsync(byte[] data)
    {
        Interlocked.Increment(ref SendAttempts);
        var error = SendError;
        if (error != null)
        {
            return Task.FromException(error);
        }
        lock (_gate) Sent.Add(data);
        return Task.CompletedTask;
    }

    public Task CloseAsync()
    {
        Interlocked.Increment(ref CloseCount);
        Closed?.Invoke();
        return Task.CompletedTask;
    }

    public void Dispose() => OnDispose?.Invoke();

    /// <summary>Simulate the socket dying without a client-side close.</summary>
    public void SimulateRemoteClose() => Closed?.Invoke();

    /// <summary>Server → client push, framed.</summary>
    public void ServerPacket(Packet packet) =>
        BinaryMessage?.Invoke(FrameCodec.Encode(packet.ToByteArray()));

    /// <summary>Last client → server frame, decoded back into a Packet.</summary>
    public Packet LastSentPacket()
    {
        byte[] last;
        lock (_gate) last = Sent.Last();
        return Packet.Parser.ParseFrom(new FrameDecoder().Feed(last)[0]);
    }
}

/// <summary>The client ↔ transport glue arms: fire-and-forget send on a live
/// connection, RawSend's own closed-guard under a status that still reads
/// Connected, the heartbeat swallowing a dead socket, and both kicked arms of
/// the close/open-failure paths.</summary>
public class ChirpClientGlueTests
{
    private static Packet KickPacket() => new Packet { MsgId = MsgID.KickNotify, Sequence = 0 };

    private static ChirpClient NewClient(Func<string, IChirpTransport> factory) =>
        new ChirpClient("ws://glue.test", factory, new ChirpClientOptions
        {
            RequestTimeoutMs = 10_000,
            // The periodic ping must not race these tests.
            HeartbeatIntervalMs = 60_000,
        });

    [Fact]
    public async Task Send_FireAndForget_WritesFrameWhileConnected()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        client.Send(MsgID.MessageAck, new byte[] { 7, 7, 7 });

        var sent = transport.LastSentPacket();
        Assert.Equal(MsgID.MessageAck, sent.MsgId);
        Assert.Equal(new byte[] { 7, 7, 7 }, sent.Body.ToByteArray());
        Assert.Equal(ConnStatus.Connected, client.Status);
    }

    [Fact]
    public async Task RawSend_GuardCatchesTransportClearedUnderConnectedStatus()
    {
        var first = new GlueTransport();
        var second = new GlueTransport();
        var queue = new Queue<IChirpTransport>(new IChirpTransport[] { first, second });
        var client = NewClient(_ => queue.Dequeue());
        await client.ConnectAsync();

        RequestError? caught = null;
        first.OnDispose = () =>
        {
            // ConnectAsync nulls _transport before it flips status to
            // Connecting: Send()'s outer guard still sees Connected, so the
            // closed-check inside RawSend is the one that fires.
            try
            {
                client.Send(MsgID.MessageAck, new byte[] { 1 });
            }
            catch (RequestError error)
            {
                caught = error;
            }
        };

        await client.ConnectAsync();

        Assert.NotNull(caught);
        Assert.Equal(RequestErrorKind.Closed, caught!.Kind);
        Assert.Equal(ConnStatus.Connected, client.Status);
    }

    [Fact]
    public async Task Heartbeat_SwallowsSendFailureAndStaysConnected()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        transport.SendError = new IOException("scripted socket death");
        client.HeartbeatNow(); // → SendPing → RawSend → faulted SendAsync → swallowed

        Assert.Equal(1, transport.SendAttempts);
        Assert.Empty(transport.Sent);
        Assert.Equal(ConnStatus.Connected, client.Status);
    }

    [Fact]
    public async Task TransportClosed_WhileKicked_StaysInKicked()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        client.StatusChanged += status =>
        {
            // Reenter from MarkKicked's SetStatus: it flips the status before
            // tearing the subscription down, so a close racing a kick lands in
            // OnTransportClosed with _kicked already set.
            if (status == ConnStatus.Kicked)
            {
                transport.SimulateRemoteClose();
            }
        };

        transport.ServerPacket(KickPacket());
        await FakeTransport.SettleAsync();

        Assert.Equal(ConnStatus.Kicked, client.Status);
        Assert.True(client.Kicked);
        Assert.Equal(1, transport.CloseCount); // kick teardown still closed the socket
    }

    [Fact]
    public async Task ConnectFailure_AfterMidOpenKick_KeepsKicked()
    {
        var first = new GlueTransport();
        var openGate = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = new GlueTransport { OpenGate = openGate.Task };
        var queue = new Queue<IChirpTransport>(new IChirpTransport[] { first, second });
        var client = NewClient(_ => queue.Dequeue());
        await client.ConnectAsync();

        var connecting = client.ConnectAsync(); // parked on second's open gate

        // ConnectAsync unsubscribes the stale transport's Closed event only;
        // its BinaryMessage subscription stays live, so a kick arriving
        // mid-open flips _kicked while the open attempt is still pending.
        first.ServerPacket(KickPacket());
        Assert.Equal(ConnStatus.Kicked, client.Status);

        // The open then fails and HandleClose runs with the kick already set.
        openGate.SetException(new IOException("scripted open failure"));
        var error = await Assert.ThrowsAsync<RequestError>(() => connecting);

        Assert.Equal(RequestErrorKind.Closed, error.Kind);
        Assert.Equal(ConnStatus.Kicked, client.Status);
        Assert.True(client.Kicked);
    }

    // ---- search(2248/2249) + 群昵称(2122-2124) 对拍(与 C++ core/TS 同形) ----

    [Fact]
    public async Task SearchMessagesAsync_RoundTrip_FullOptionSurface()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        var pending = client.SearchMessagesAsync("needle", channelId: "c1",
            contentTypes: new[] { (int)Chirp.Chat.MsgType.Text }, beforeTimestamp: 1234,
            beforeMessageId: "m-10", limit: 5);
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.SearchMessageReq, request.MsgId);
        var req = Chirp.Chat.SearchMessageRequest.Parser.ParseFrom(request.Body);
        Assert.Equal("needle", req.Keyword);
        Assert.Equal("c1", req.ChannelId);
        Assert.Equal(new[] { (int)Chirp.Chat.MsgType.Text }, req.ContentTypes);
        Assert.Equal(1234, req.BeforeTimestamp);
        Assert.Equal("m-10", req.BeforeMessageId);
        Assert.Equal(5, req.Limit);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SearchMessageResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.SearchMessageResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Matches = { new Chirp.Chat.SearchMessageMatch { MessageId = "m-9", ChannelId = "c1" } },
                HasMore = true,
            }.ToByteArray()),
        });

        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.Single(resp.Matches);
        Assert.Equal("m-9", resp.Matches[0].MessageId);
        Assert.Equal("c1", resp.Matches[0].ChannelId);
        Assert.True(resp.HasMore);
    }

    [Fact]
    public async Task SetMemberAliasAsync_RoundTrip_AliasAndClearPassthrough()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        var pending = client.SetMemberAliasAsync("g1", "u2", "队长");
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.SetMemberAliasReq, request.MsgId);
        var req = Chirp.Chat.SetMemberAliasRequest.Parser.ParseFrom(request.Body);
        Assert.Equal("g1", req.GroupId);
        Assert.Equal("u2", req.TargetUserId);
        Assert.Equal("队长", req.Alias);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SetMemberAliasResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.SetMemberAliasResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                GroupId = "g1",
                UserId = "u2",
                Alias = "队长",
            }.ToByteArray()),
        });
        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.Equal("u2", resp.UserId);
        Assert.Equal("队长", resp.Alias);
    }

    [Fact]
    public async Task SetGroupMuteAsync_RoundTrip_MuteAndUnmutePassthrough()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        var pending = client.SetGroupMuteAsync("g1", "u2", 3600);
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.SetGroupMuteReq, request.MsgId);
        var req = Chirp.Chat.SetGroupMuteRequest.Parser.ParseFrom(request.Body);
        Assert.Equal("g1", req.GroupId);
        Assert.Equal("u2", req.TargetUserId);
        Assert.Equal(3600, req.DurationSec);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SetGroupMuteResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.SetGroupMuteResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                GroupId = "g1",
                UserId = "u2",
                MutedUntilTs = 1700000000000,
            }.ToByteArray()),
        });
        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.Equal("u2", resp.UserId);
        Assert.Equal(1700000000000, resp.MutedUntilTs);

        // 解禁（durationSec=0）原样透传。
        var unmute = client.SetGroupMuteAsync("g1", "u2", 0);
        var unmuteReq = Chirp.Chat.SetGroupMuteRequest.Parser.ParseFrom(
            transport.LastSentPacket().Body);
        Assert.Equal(0, unmuteReq.DurationSec);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SetGroupMuteResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.SetGroupMuteResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                MutedUntilTs = 0,
            }.ToByteArray()),
        });
        Assert.Equal(Chirp.Common.ErrorCode.Ok, (await unmute).Code);
    }

    [Fact]
    public async Task SearchMessagesAsync_ValidationOrder_ConnectionThenKeyword()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        // 连接态检查先于参数校验（与 C++ core/TS 同序）。
        await Assert.ThrowsAsync<RequestError>(() => client.SearchMessagesAsync("needle"));

        await client.ConnectAsync();
        await Assert.ThrowsAsync<ArgumentException>(() => client.SearchMessagesAsync(""));
        Assert.Empty(transport.Sent); // 空 keyword 不发包
    }

    [Fact]
    public async Task GroupAliasNotify_DecodesAndDispatchesHook_MalformedIgnored()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        var (groupId, userId, alias) = (null as string, null as string, null as string);
        client.AddListener(new RecordingHookListener
        {
            OnAlias = (g, u, a) => { groupId = g; userId = u; alias = a; },
        });

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GroupMemberAliasUpdatedNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.GroupMemberAliasUpdatedNotify
            {
                GroupId = "g1",
                UserId = "u2",
                Alias = "nick",
            }.ToByteArray()),
        });
        await FakeTransport.SettleAsync();
        Assert.Equal(("g1", "u2", "nick"), (groupId, userId, alias));

        // 不可解码体：静默丢弃，监听器不炸。
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GroupMemberAliasUpdatedNotify,
            Body = ByteString.CopyFrom(new byte[] { 0xff, 0x01 }),
        });
        await FakeTransport.SettleAsync();
        Assert.Equal(("g1", "u2", "nick"), (groupId, userId, alias));
    }

    [Fact]
    public async Task GroupMemberMutedNotify_DecodesAndDispatchesHook_MalformedIgnored()
    {
        var transport = new GlueTransport();
        var client = NewClient(_ => transport);
        await client.ConnectAsync();

        var (groupId, userId, mutedUntilTs, operatorId) =
            (null as string, null as string, 0L, null as string);
        client.AddListener(new RecordingHookListener
        {
            OnMute = (g, u, until, op) => { groupId = g; userId = u; mutedUntilTs = until; operatorId = op; },
        });

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GroupMemberMutedNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.GroupMemberMutedNotify
            {
                GroupId = "g1",
                UserId = "u2",
                MutedUntilTs = 1700000000000,
                OperatorId = "mod1",
            }.ToByteArray()),
        });
        await FakeTransport.SettleAsync();
        Assert.Equal(("g1", "u2", 1700000000000L, "mod1"), (groupId, userId, mutedUntilTs, operatorId));

        // 不可解码体：静默丢弃，监听器不炸。
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GroupMemberMutedNotify,
            Body = ByteString.CopyFrom(new byte[] { 0xff, 0x01 }),
        });
        await FakeTransport.SettleAsync();
        Assert.Equal(("g1", "u2", 1700000000000L, "mod1"), (groupId, userId, mutedUntilTs, operatorId));
    }

    /// <summary>Hook-surface recorder: alias and mute notifies are observed.</summary>
    private sealed class RecordingHookListener : IChatEventListener
    {
        public Action<string, string, string>? OnAlias { get; set; }
        public Action<string, string, long, string>? OnMute { get; set; }

        public void OnGroupMemberAliasUpdated(string groupId, string userId, string alias) =>
            OnAlias?.Invoke(groupId, userId, alias);

        public void OnGroupMemberMutedUpdated(string groupId, string userId, long mutedUntilTs,
            string operatorId) =>
            OnMute?.Invoke(groupId, userId, mutedUntilTs, operatorId);
    }
}
