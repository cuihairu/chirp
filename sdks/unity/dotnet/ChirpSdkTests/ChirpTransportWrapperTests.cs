using System;
using System.Collections.Generic;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Sdk;
using Xunit;

/// <summary>Scripted IChirpSocket: the wrapper-test harness beneath
/// ClientWebSocketTransport. The loopback suite (ChirpTransportTests) drives
/// the transport against a real socket; this fake scripts the socket itself
/// so the arms a well-behaved peer never reaches are deterministic:
/// a close handshake whose reply throws, and a client-initiated graceful
/// close while the socket is still open.</summary>
internal sealed class ScriptedSocket : IChirpSocket
{
    private sealed class ReceiveScript
    {
        public byte[]? Data;
        public WebSocketMessageType Type;
        public bool EndOfMessage;
    }

    private readonly Queue<ReceiveScript> _receives = new Queue<ReceiveScript>();

    public WebSocketState State { get; private set; } = WebSocketState.None;
    public bool CloseThrows { get; set; }
    public int ConnectCount { get; private set; }
    public int CloseCount { get; private set; }
    public int SendCount { get; private set; }
    public WebSocketCloseStatus? LastCloseStatus { get; private set; }
    public bool Disposed { get; private set; }

    public ScriptedSocket EnqueueBinary(byte[] payload)
    {
        _receives.Enqueue(new ReceiveScript
        {
            Data = payload,
            Type = WebSocketMessageType.Binary,
            EndOfMessage = true,
        });
        return this;
    }

    /// <summary>A non-final fragment: the next receive continues the same
    /// message, which is how the loop's buffering arms get driven.</summary>
    public ScriptedSocket EnqueueFragment(byte[] payload)
    {
        _receives.Enqueue(new ReceiveScript
        {
            Data = payload,
            Type = WebSocketMessageType.Binary,
            EndOfMessage = false,
        });
        return this;
    }

    public ScriptedSocket EnqueueClose()
    {
        _receives.Enqueue(new ReceiveScript
        {
            Type = WebSocketMessageType.Close,
            EndOfMessage = true,
        });
        return this;
    }

    public Task ConnectAsync(Uri uri, CancellationToken ct)
    {
        ConnectCount++;
        State = WebSocketState.Open;
        return Task.CompletedTask;
    }

    public Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (_receives.Count > 0)
        {
            var script = _receives.Dequeue();
            if (script.Data != null)
            {
                Buffer.BlockCopy(script.Data, 0, buffer.Array!, buffer.Offset, script.Data.Length);
            }
            return Task.FromResult(new WebSocketReceiveResult(
                script.Data?.Length ?? 0, script.Type, script.EndOfMessage));
        }
        // Script exhausted: park like a socket waiting for the next frame,
        // and let the transport's loop token end the wait.
        var parked = new TaskCompletionSource<WebSocketReceiveResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (ct.CanBeCanceled)
        {
            ct.Register(() => parked.TrySetCanceled(ct));
        }
        return parked.Task;
    }

    public Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType,
        bool endOfMessage, CancellationToken ct)
    {
        SendCount++;
        return Task.CompletedTask;
    }

    public Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription,
        CancellationToken ct)
    {
        CloseCount++;
        LastCloseStatus = closeStatus;
        if (CloseThrows)
        {
            return Task.FromException(new InvalidOperationException("scripted close failure"));
        }
        State = WebSocketState.Closed;
        return Task.CompletedTask;
    }

    public void Dispose() => Disposed = true;
}

/// <summary>ClientWebSocketTransport wired through its IChirpSocket seam with
/// a scripted socket underneath.</summary>
internal sealed class TransportHarness : IDisposable
{
    private readonly TaskCompletionSource<bool> _closed =
        new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource<byte[]> _message =
        new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);

    public ScriptedSocket Socket { get; } = new ScriptedSocket();
    public ClientWebSocketTransport Transport { get; }
    public int ClosedCount;

    public TransportHarness()
    {
        Transport = new ClientWebSocketTransport(() => Socket);
        Transport.Closed += () =>
        {
            Interlocked.Increment(ref ClosedCount);
            _closed.TrySetResult(true);
        };
        Transport.BinaryMessage += message => _message.TrySetResult(message);
    }

    public Task<byte[]> MessageAsync => _message.Task;

    public Task OpenAsync() => Transport.OpenAsync("ws://scripted.test/", CancellationToken.None);

    public async Task AwaitClosedOnceAsync()
    {
        Assert.True(await _closed.Task.WaitAsync(TimeSpan.FromSeconds(5)),
            "Closed never fired");
        // The guard is per-instance, so a second raise is impossible after
        // this point only if the count is already exactly one.
        Assert.Equal(1, Volatile.Read(ref ClosedCount));
    }

    public void Dispose() => Transport.Dispose();
}

/// <summary>Wrapper-harness coverage for ClientWebSocketTransport: the two
/// close-handshake paths (receive-side reply that throws, client-initiated
/// graceful close that succeeds or throws) that the real-socket loopback
/// cannot script.</summary>
public class ChirpTransportWrapperTests
{
    [Fact]
    public async Task ServerClose_WithThrowingHandshakeReply_StillClosesOnce()
    {
        using var harness = new TransportHarness();
        harness.Socket.CloseThrows = true;
        harness.Socket.EnqueueClose();

        await harness.OpenAsync();
        await harness.AwaitClosedOnceAsync();

        // The reply handshake was attempted (and failed) before the loop
        // treated the close frame as final: best effort, not skipped.
        Assert.Equal(1, harness.Socket.CloseCount);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, harness.Socket.LastCloseStatus);
        Assert.Equal(0, harness.Socket.SendCount);
    }

    [Fact]
    public async Task CloseAsync_WhileOpen_RunsGracefulHandshake()
    {
        using var harness = new TransportHarness();
        await harness.OpenAsync();

        await harness.Transport.CloseAsync();
        await harness.AwaitClosedOnceAsync();

        Assert.Equal(1, harness.Socket.CloseCount);
        Assert.Equal(WebSocketCloseStatus.NormalClosure, harness.Socket.LastCloseStatus);
        Assert.Equal(WebSocketState.Closed, harness.Socket.State);
        Assert.True(harness.Socket.Disposed);
        Assert.Equal(1, harness.ClosedCount);
    }

    [Fact]
    public async Task CloseAsync_WhenHandshakeThrows_StillDisposesAndRaisesClosed()
    {
        using var harness = new TransportHarness();
        harness.Socket.CloseThrows = true;
        await harness.OpenAsync();

        await harness.Transport.CloseAsync();
        await harness.AwaitClosedOnceAsync();

        Assert.Equal(1, harness.Socket.CloseCount);
        Assert.True(harness.Socket.Disposed);
    }

    [Fact]
    public async Task Send_BeforeOpen_AndAfterClose_ThrowsClosed()
    {
        // The seam must not change the transport's send guards: not-open
        // still surfaces as RequestError(Closed) on both sides of a session.
        using var harness = new TransportHarness();
        var beforeOpen = await Assert.ThrowsAsync<RequestError>(
            () => harness.Transport.SendAsync(new byte[] { 1 }));
        Assert.Equal(RequestErrorKind.Closed, beforeOpen.Kind);

        await harness.OpenAsync();
        await harness.Transport.CloseAsync();

        var afterClose = await Assert.ThrowsAsync<RequestError>(
            () => harness.Transport.SendAsync(new byte[] { 2 }));
        Assert.Equal(RequestErrorKind.Closed, afterClose.Kind);
    }

    [Fact]
    public async Task FragmentedMessage_FillingBuffer_GrowsItAndDelivers()
    {
        using var harness = new TransportHarness();
        // Two half-buffer fragments fill the 64 KiB receive buffer exactly;
        // the loop must double it before the final fragment can complete the
        // message.
        harness.Socket.EnqueueFragment(new byte[32 * 1024]);
        harness.Socket.EnqueueFragment(new byte[32 * 1024]);
        harness.Socket.EnqueueBinary(new byte[] { 9, 9 });

        await harness.OpenAsync();
        var message = await harness.MessageAsync.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(64 * 1024 + 2, message.Length);
        Assert.Equal(9, message[^1]);
    }

    [Fact]
    public async Task MessageOverSizeCap_ThrowsAndClosesWithoutDelivery()
    {
        using var harness = new TransportHarness();
        harness.Socket.EnqueueFragment(new byte[ClientWebSocketTransportSizeProbe.Cap]);
        harness.Socket.EnqueueFragment(new byte[8]);

        await harness.OpenAsync();
        await harness.AwaitClosedOnceAsync();

        // The cap fired before any message could be handed up.
        Assert.False(harness.MessageAsync.IsCompleted);
    }

    private static class ClientWebSocketTransportSizeProbe
    {
        // Mirrors the transport's private MaxMessageBytes (17 MiB): the second
        // fragment pushes filled past it, tripping the size-cap throw.
        public const int Cap = 17 * 1024 * 1024;
    }
}
