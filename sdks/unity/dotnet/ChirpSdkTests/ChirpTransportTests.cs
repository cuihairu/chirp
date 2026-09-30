using System;
using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Sdk;
using Xunit;

/// <summary>ClientWebSocketTransport against a real loopback WebSocket
/// server (HttpListener upgrade): fragmentation aggregation, the 17MB cap,
/// close handshakes from both ends, and the not-open send guard. The rest
/// of the suite drives IChirpTransport through fakes; these tests exercise
/// the production adapter dotnet CI actually ships.</summary>
public class ChirpTransportTests
{
    private static int FreePort()
    {
        var probe = new TcpListener(IPAddress.Loopback, 0);
        probe.Start();
        var port = ((IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop();
        return port;
    }

    /// <summary>The server side of one accepted ws pair.</summary>
    private sealed class ServerSide : IDisposable
    {
        public readonly HttpListener Listener;
        public readonly WebSocket Socket;

        public ServerSide(HttpListener listener, WebSocket socket)
        {
            Listener = listener;
            Socket = socket;
        }

        public void Dispose()
        {
            Socket.Dispose();
            Listener.Stop();
            ((IDisposable)Listener).Dispose();
        }
    }

    /// <summary>Handshake helper: opens the transport against a fresh
    /// loopback server and returns both ends plus the first-message and
    /// closed signals (each fires once).</summary>
    private static async Task<(ServerSide Server, ClientWebSocketTransport Transport,
        TaskCompletionSource<byte[]> Message, TaskCompletionSource<object?> Closed)> ConnectAsync()
    {
        var port = FreePort();
        var listener = new HttpListener();
        listener.Prefixes.Add($"http://127.0.0.1:{port}/");
        listener.Start();
        var accept = AcceptOneAsync(listener);

        var transport = new ClientWebSocketTransport();
        var message = new TaskCompletionSource<byte[]>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var closed = new TaskCompletionSource<object?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        transport.BinaryMessage += b => message.TrySetResult(b);
        transport.Closed += () => closed.TrySetResult(null);

        await transport.OpenAsync($"ws://127.0.0.1:{port}/", CancellationToken.None)
            .ConfigureAwait(false);
        var server = await accept.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        return (new ServerSide(listener, server), transport, message, closed);
    }

    private static async Task<WebSocket> AcceptOneAsync(HttpListener listener)
    {
        var ctx = await listener.GetContextAsync().ConfigureAwait(false);
        Assert.True(ctx.Request.IsWebSocketRequest, "client must speak ws");
        var wsCtx = await ctx.AcceptWebSocketAsync(null).ConfigureAwait(false);
        return wsCtx.WebSocket;
    }

    [Fact]
    public async Task SendBeforeOpen_ThrowsClosed()
    {
        using var transport = new ClientWebSocketTransport();
        var err = await Assert.ThrowsAsync<RequestError>(
            () => transport.SendAsync(new byte[] { 1 }));
        Assert.Equal(RequestErrorKind.Closed, err.Kind);
    }

    [Fact]
    public async Task OpenToDeadPort_Throws()
    {
        using var transport = new ClientWebSocketTransport();
        var port = FreePort(); // nothing listening here
        await Assert.ThrowsAnyAsync<Exception>(
            () => transport.OpenAsync($"ws://127.0.0.1:{port}/", CancellationToken.None));
    }

    [Fact]
    public async Task FragmentedMessage_Aggregated_AcrossBufferGrowth()
    {
        // 70KB in 8KB fragments: the transport's 64KB read buffer must grow
        // mid-message and still deliver exactly one message.
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        using (transport)
        {
            var payload = new byte[70_000];
            for (var i = 0; i < payload.Length; i++) payload[i] = (byte)(i % 251);
            const int chunk = 8192;
            for (var off = 0; off < payload.Length; off += chunk)
            {
                var n = Math.Min(chunk, payload.Length - off);
                var last = off + n == payload.Length;
                await server.Socket.SendAsync(
                    new ArraySegment<byte>(payload, off, n),
                    WebSocketMessageType.Binary, last, CancellationToken.None);
            }
            var got = await message.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Equal(payload, got);
            Assert.False(closed.Task.IsCompleted);
        }
    }

    [Fact]
    public async Task SendAsync_DeliversBytesToServer()
    {
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        using (transport)
        {
            var payload = new byte[] { 0xDE, 0xAD, 0xBE, 0xEF };
            await transport.SendAsync(payload);
            var buf = new byte[64];
            var result = await server.Socket.ReceiveAsync(
                new ArraySegment<byte>(buf), CancellationToken.None)
                .WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Equal(WebSocketMessageType.Binary, result.MessageType);
            Assert.True(result.EndOfMessage);
            Assert.Equal(payload.Length, result.Count);
            Assert.Equal(payload, buf[..result.Count]);
        }
    }

    [Fact]
    public async Task ServerClose_HandshakeCompletes_ClosedFires()
    {
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        using (transport)
        {
            await server.Socket.CloseOutputAsync(
                WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
            // The transport answers the close handshake itself, then raises
            // Closed exactly once.
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        }
    }

    [Fact]
    public async Task ClientClose_Graceful_ClosedOnce_SendAfterClosedThrows()
    {
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        using (transport)
        {
            await transport.CloseAsync();
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5));
            var err = await Assert.ThrowsAsync<RequestError>(
                () => transport.SendAsync(new byte[] { 1 }));
            Assert.Equal(RequestErrorKind.Closed, err.Kind);
        }
    }

    [Fact]
    public async Task OversizeMessage_KillsLoop_ClosedFires()
    {
        // 18 one-MB fragments, none final: the aggregate passes the 17MB cap
        // and the loop must die (Closed) instead of buffering forever.
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        using (transport)
        {
            var fragment = new byte[1024 * 1024];
            var pushing = Task.Run(async () =>
            {
                try
                {
                    for (var i = 0; i < 18; i++)
                    {
                        await server.Socket.SendAsync(
                            new ArraySegment<byte>(fragment),
                            WebSocketMessageType.Binary, false, CancellationToken.None)
                            .ConfigureAwait(false);
                    }
                }
                catch (Exception)
                {
                    // The client kills the socket as soon as it passes the
                    // cap; remaining pushes fail. Expected.
                }
            });
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(10));
            await pushing.WaitAsync(TimeSpan.FromSeconds(10));
        }
    }

    [Fact]
    public async Task Dispose_StopsLoop_SendAfterDisposeThrows()
    {
        var (server, transport, message, closed) = await ConnectAsync();
        using (server)
        {
            transport.Dispose();
            var err = await Assert.ThrowsAsync<RequestError>(
                () => transport.SendAsync(new byte[] { 1 }));
            Assert.Equal(RequestErrorKind.Closed, err.Kind);
        }
    }
}
