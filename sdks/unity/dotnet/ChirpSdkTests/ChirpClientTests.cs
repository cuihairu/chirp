using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>Scriptable in-memory transport: the test plays the server side.</summary>
internal sealed class FakeTransport : IChirpTransport
{
    public event Action<byte[]>? BinaryMessage;
    public event Action? Closed;

    public readonly List<byte[]> Sent = new();
    private readonly object _gate = new();
    public int CloseCount;

    public Task OpenAsync(string url, CancellationToken ct) => Task.CompletedTask;

    public Task SendAsync(byte[] data)
    {
        lock (_gate) Sent.Add(data);
        return Task.CompletedTask;
    }

    public async Task CloseAsync()
    {
        Interlocked.Increment(ref CloseCount);
        Closed?.Invoke();
        await Task.CompletedTask;
    }

    public void Dispose() { }

    /// <summary>Simulate the socket dying without a client-side close.</summary>
    public void SimulateRemoteClose() => Closed?.Invoke();

    /// <summary>Server → client push, framed.</summary>
    public void ServerPacket(Packet packet)
    {
        BinaryMessage?.Invoke(FrameCodec.Encode(packet.ToByteArray()));
    }

    /// <summary>Last client → server frame, decoded back into a Packet.</summary>
    public Packet LastSentPacket()
    {
        byte[] last;
        lock (_gate) last = Sent.Last();
        return Packet.Parser.ParseFrom(FrameDecoderFeed(last));
    }

    private static byte[] FrameDecoderFeed(byte[] frame)
    {
        var decoder = new FrameDecoder();
        return decoder.Feed(frame).Single();
    }

    /// <summary>Let queued client continuations run to completion.</summary>
    public static async Task SettleAsync()
    {
        for (var i = 0; i < 5; i++)
        {
            await Task.Yield();
            await Task.Delay(10);
        }
    }
}

internal static class TestClient
{
    public static ChirpClient Connected(FakeTransport transport, out ChirpClient client)
    {
        client = new ChirpClient(
            "ws://test",
            transportFactory: _ => transport,
            options: new ChirpClientOptions { RequestTimeoutMs = 10_000 });
        return client;
    }
}

public class ChirpClientTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    [Fact]
    public async Task Connect_ReachesConnected_AndFiresStatusListeners()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        var statuses = new List<ConnStatus>();
        client.StatusChanged += statuses.Add;

        await client.ConnectAsync();

        Assert.Equal(ConnStatus.Connected, client.Status);
        Assert.Equal(new[] { ConnStatus.Connecting, ConnStatus.Connected }, statuses);
    }

    [Fact]
    public async Task RequestResponse_CorrelatesBySequence()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.SendMessage, new Chirp.Chat.SendMessageRequest { SenderId = "u1" });
        await Settle();
        var request = transport.LastSentPacket();
        Assert.Equal(1, request.Sequence);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SendMessageResp,
            Sequence = 1,
            Body = ByteString.CopyFrom(
                new Chirp.Chat.SendMessageResponse { Code = Chirp.Common.ErrorCode.Ok, MessageId = "m1" }.ToByteArray()),
        });

        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.Equal("m1", resp.MessageId);

        var sent = transport.LastSentPacket();
        Assert.Equal(MsgID.SendMessageReq, sent.MsgId);
        Assert.Equal(
            new Chirp.Chat.SendMessageRequest { SenderId = "u1" }.ToByteArray(),
            sent.Body.ToByteArray());
    }

    [Fact]
    public async Task Request_TimesOut_AndLateResponseIsDroppedHarmlessly()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.GetHistory, new Chirp.Chat.GetHistoryRequest { UserId = "u1" }, timeoutMs: 10);

        var error = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(RequestErrorKind.Timeout, error.Kind);

        // The straggler now finds no pending entry and must not crash.
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GetHistoryResp,
            Sequence = 99,
            Body = ByteString.CopyFrom(new Chirp.Chat.GetHistoryResponse().ToByteArray()),
        });
        await Settle();
    }

    [Fact]
    public async Task NotifyPackets_SequenceZero_DispatchToSubscribers()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var bodies = new List<byte[]>();
        var off = client.OnNotify(MsgID.ChatMessageNotify, bodies.Add);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.ChatMessage { MessageId = "m9" }.ToByteArray()),
        });
        await Settle();
        Assert.Single(bodies);
        Assert.Equal("m9", Chirp.Chat.ChatMessage.Parser.ParseFrom(bodies.Single()).MessageId);

        off();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.ChatMessage { MessageId = "m10" }.ToByteArray()),
        });
        await Settle();
        Assert.Single(bodies);
    }

    [Fact]
    public async Task KickNotify_IsTerminal_PendingRejected_NoReconnect()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.Login, new Chirp.Auth.LoginRequest { Token = "u1", DeviceId = "d1" });
        await Settle();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.KickNotify,
            Body = ByteString.CopyFrom(new Chirp.Auth.KickNotify { Reason = "device takeover" }.ToByteArray()),
        });

        var error = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(RequestErrorKind.Kicked, error.Kind);
        await Settle();
        Assert.True(client.Kicked);
        Assert.Equal(ConnStatus.Kicked, client.Status);
        Assert.Equal(1, transport.CloseCount);
    }

    [Fact]
    public async Task HeartbeatPingPong_TracksClockOffset()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        Assert.Null(client.ClockOffsetMs);

        client.HeartbeatNow();
        await Settle();
        var ping = transport.LastSentPacket();
        Assert.Equal(MsgID.HeartbeatPing, ping.MsgId);

        // The pong must echo the ping's non-zero sequence, else the client
        // treats it as a notify and drops it.
        var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.HeartbeatPong,
            Sequence = ping.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Gateway.HeartbeatPong { ServerTime = now }.ToByteArray()),
        });
        await Settle();
        Assert.NotNull(client.ClockOffsetMs);
        Assert.InRange(Math.Abs(client.ClockOffsetMs!.Value), 0, 2000);
    }

    [Fact]
    public async Task TransportClose_WaitsToReconnect_DisconnectStopsEverything()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        // Simulate the socket dying underneath us (remote close): the client
        // did not call CloseAsync, so CloseCount stays 0 while the event fires.
        transport.SimulateRemoteClose();
        await Settle();
        Assert.Equal(ConnStatus.WaitingReconnect, client.Status);

        // A reconnect was scheduled; Disconnect() must cancel it cleanly.
        client.Disconnect();
        Assert.Equal(ConnStatus.Closed, client.Status);
        await Task.Delay(600); // reconnect base backoff would have fired
        Assert.Equal(ConnStatus.Closed, client.Status);
    }

    [Fact]
    public async Task AutoReconnect_FiresReconnectingReconnected_AndRestoresConnected()
    {
        var transports = new List<FakeTransport>();
        var client = new ChirpClient(
            "ws://test",
            transportFactory: _ =>
            {
                var t = new FakeTransport();
                transports.Add(t);
                return t;
            },
            options: new ChirpClientOptions
            {
                RequestTimeoutMs = 10_000,
                ReconnectBaseMs = 50,
                ReconnectMaxMs = 100,
            });
        await client.ConnectAsync();
        Assert.Single(transports);

        var reconnecting = new List<(int Attempt, int DelayMs)>();
        var reconnected = new List<bool>();
        client.Reconnecting += (attempt, delayMs) => reconnecting.Add((attempt, delayMs));
        client.Reconnected += () => reconnected.Add(true);

        transports[0].SimulateRemoteClose();

        // The backoff (50ms) may elapse during settle alone; assert the end
        // state, not the mid-states (covered by the other lifecycle tests).
        await Task.Delay(800);
        Assert.Equal(ConnStatus.Connected, client.Status);
        Assert.Equal(2, transports.Count);
        Assert.Equal(new[] { 1 }, reconnecting.Select(r => r.Attempt).ToArray());
        Assert.InRange(reconnecting[0].DelayMs, 1, 1000);
        Assert.Single(reconnected);
    }

    [Fact]
    public async Task DisconnectDuringBackoff_ReconnectingFires_ReconnectedNeverDoes()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var reconnected = 0;
        client.Reconnected += () => reconnected++;

        transport.SimulateRemoteClose();
        await Settle();
        client.Disconnect();
        await Task.Delay(600);
        Assert.Equal(ConnStatus.Closed, client.Status);
        Assert.Equal(0, reconnected);
    }

    [Fact]
    public async Task Kicked_ClientRecoversThroughExplicitConnect()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.KickNotify,
            Body = ByteString.CopyFrom(new Chirp.Auth.KickNotify { Reason = "takeover" }.ToByteArray()),
        });
        await Settle();
        Assert.Equal(ConnStatus.Kicked, client.Status);

        // An explicit connect is a user action: it clears the terminal kick.
        await client.ConnectAsync();
        Assert.Equal(ConnStatus.Connected, client.Status);
        Assert.False(client.Kicked);
    }

    [Fact]
    public async Task RemoteClose_RejectsPendingRequestsWithClosed()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.SendMessage, new Chirp.Chat.SendMessageRequest { SenderId = "u1" });
        await Settle();
        transport.SimulateRemoteClose();

        var error = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(RequestErrorKind.Closed, error.Kind);
    }

    [Fact]
    public async Task VoiceSpec_RoundTripsJoinRoom()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.JoinVoiceRoom, new Chirp.Voice.JoinRoomRequest { UserId = "u1", RoomId = "r1" });
        await Settle();
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.JoinRoomReq, request.MsgId);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.JoinRoomResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Voice.JoinRoomResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                RoomId = "r1",
                SdpAnswer = "v=answer",
            }.ToByteArray()),
        });

        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.Equal("r1", resp.RoomId);
        Assert.Equal("v=answer", resp.SdpAnswer);
        Assert.Equal(new[] { "u1", "r1" },
            new[] { Chirp.Voice.JoinRoomRequest.Parser.ParseFrom(request.Body.ToByteArray()).UserId,
                    Chirp.Voice.JoinRoomRequest.Parser.ParseFrom(request.Body.ToByteArray()).RoomId });
    }
}
