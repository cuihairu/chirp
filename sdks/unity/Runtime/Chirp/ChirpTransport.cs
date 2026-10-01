using System;
using System.Net.WebSockets;
using System.Threading;
using System.Threading.Tasks;

namespace Chirp.Sdk
{
    /// <summary>
    /// Minimal transport seam between the connection state machine and the
    /// platform socket, mirroring the web/mobile ports' WebSocket injection
    /// point. Tests inject a scripted fake; production uses
    /// <see cref="ClientWebSocketTransport"/>.
    /// </summary>
    public interface IChirpTransport : IDisposable
    {
        /// <summary>One complete WebSocket binary message (server → client).</summary>
        event Action<byte[]>? BinaryMessage;

        /// <summary>Fires when the socket is down — after <see cref="CloseAsync"/>,
        /// after a remote close, or after an error.</summary>
        event Action? Closed;

        /// <summary>Opens the socket; completes when open, throws when the
        /// attempt failed.</summary>
        Task OpenAsync(string url, CancellationToken ct);

        Task SendAsync(byte[] data);

        /// <summary>Best-effort graceful close; <see cref="Closed"/> still fires.</summary>
        Task CloseAsync();
    }

    /// <summary>Socket wrapper seam under <see cref="ClientWebSocketTransport"/>:
    /// the adapter forwards every call to a <see cref="ClientWebSocket"/>, and
    /// tests inject a scripted socket instead (the same way the web/mobile
    /// ports wrap their platform socket). Keeping the seam at this level means
    /// the receive loop, the size cap and both close-handshake paths are
    /// driven deterministically without a live TCP peer.</summary>
    public interface IChirpSocket : IDisposable
    {
        WebSocketState State { get; }

        Task ConnectAsync(Uri uri, CancellationToken ct);

        Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer, CancellationToken ct);

        Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType,
            bool endOfMessage, CancellationToken ct);

        Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription,
            CancellationToken ct);
    }

    /// <summary>System.Net.WebSockets adapter. Works under Unity
    /// (Mono/IL2CPP on all non-WebGL targets) and under plain dotnet, which is
    /// what the CI test run exercises. Text frames never occur on this
    /// protocol; they are treated as protocol errors.</summary>
    public sealed class ClientWebSocketTransport : IChirpTransport
    {
        // Single ws message can arrive fragmented; aggregate before handing up.
        private const int MaxMessageBytes = 17 * 1024 * 1024;

        private readonly Func<IChirpSocket> _socketFactory;
        private IChirpSocket? _ws;
        private CancellationTokenSource? _loopCts;
        private int _closedRaised;

        public event Action<byte[]>? BinaryMessage;
        public event Action? Closed;

        /// <param name="socketFactory">Overrides the socket wrapper (tests
        /// inject a scripted one); production leaves it null.</param>
        public ClientWebSocketTransport(Func<IChirpSocket>? socketFactory = null)
        {
            _socketFactory = socketFactory ?? (() => new ClientWebSocketSocket());
        }

        public async Task OpenAsync(string url, CancellationToken ct)
        {
            var ws = _socketFactory();
            _ws = ws;
            await ws.ConnectAsync(new Uri(url), ct).ConfigureAwait(false);
            _loopCts = new CancellationTokenSource();
            _ = ReceiveLoopAsync(ws, _loopCts.Token);
        }

        private async Task ReceiveLoopAsync(IChirpSocket ws, CancellationToken ct)
        {
            var buffer = new byte[64 * 1024];
            try
            {
                while (ws.State == WebSocketState.Open && !ct.IsCancellationRequested)
                {
                    var message = await ReceiveWholeMessageAsync(ws, buffer, ct).ConfigureAwait(false);
                    if (message == null) break; // close frame
                    BinaryMessage?.Invoke(message);
                }
            }
            catch (OperationCanceledException)
            {
                // Shutdown path: CloseAsync cancelled the loop; Closed was
                // already raised there.
                return;
            }
            catch (Exception)
            {
                // Socket death of any shape ends the loop; Closed fires below.
            }
            RaiseClosed();
        }

        private async Task<byte[]?> ReceiveWholeMessageAsync(
            IChirpSocket ws, byte[] buffer, CancellationToken ct)
        {
            byte[]? message = null;
            var filled = 0;
            while (true)
            {
                var result = await ws.ReceiveAsync(new ArraySegment<byte>(buffer, filled, buffer.Length - filled), ct)
                    .ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    try
                    {
                        await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None)
                            .ConfigureAwait(false);
                    }
                    catch (Exception)
                    {
                        // The peer already went away; the close handshake is
                        // best-effort.
                    }
                    return null;
                }
                filled += result.Count;
                if (filled > MaxMessageBytes)
                {
                    throw new InvalidOperationException("ws message exceeds size cap");
                }
                if (result.EndOfMessage)
                {
                    message = new byte[filled];
                    Buffer.BlockCopy(buffer, 0, message, 0, filled);
                    return message;
                }
                if (filled == buffer.Length)
                {
                    Array.Resize(ref buffer, buffer.Length * 2);
                }
            }
        }

        public async Task SendAsync(byte[] data)
        {
            var ws = _ws;
            if (ws == null || ws.State != WebSocketState.Open)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            await ws.SendAsync(
                new ArraySegment<byte>(data),
                WebSocketMessageType.Binary,
                endOfMessage: true,
                CancellationToken.None).ConfigureAwait(false);
        }

        public async Task CloseAsync()
        {
            var ws = _ws;
            _loopCts?.Cancel();
            if (ws != null && ws.State == WebSocketState.Open)
            {
                try
                {
                    await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch (Exception)
                {
                    // Best-effort: the socket may already be dead.
                }
            }
            ws?.Dispose();
            _ws = null;
            RaiseClosed();
        }

        public void Dispose()
        {
            _loopCts?.Cancel();
            _ws?.Dispose();
            _ws = null;
        }

        private void RaiseClosed()
        {
            if (Interlocked.Exchange(ref _closedRaised, 1) == 0)
            {
                Closed?.Invoke();
            }
        }

        /// <summary>Production wrapper: a plain one-shot ClientWebSocket, no
        /// retry logic — reconnect policy lives in ChirpClient.</summary>
        private sealed class ClientWebSocketSocket : IChirpSocket
        {
            private readonly ClientWebSocket _ws = new ClientWebSocket();

            public WebSocketState State => _ws.State;

            public Task ConnectAsync(Uri uri, CancellationToken ct) => _ws.ConnectAsync(uri, ct);

            public Task<WebSocketReceiveResult> ReceiveAsync(ArraySegment<byte> buffer,
                CancellationToken ct) => _ws.ReceiveAsync(buffer, ct);

            public Task SendAsync(ArraySegment<byte> buffer, WebSocketMessageType messageType,
                bool endOfMessage, CancellationToken ct) =>
                _ws.SendAsync(buffer, messageType, endOfMessage, ct);

            public Task CloseAsync(WebSocketCloseStatus closeStatus, string? statusDescription,
                CancellationToken ct) => _ws.CloseAsync(closeStatus, statusDescription, ct);

            public void Dispose() => _ws.Dispose();
        }
    }
}
