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
}
