using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Gateway;
using Google.Protobuf;

namespace Chirp.Sdk
{
    /// <summary>
    /// Connection state machine:
    ///   Idle → Connecting → Connected ⇄ WaitingReconnect (auto reconnect)
    ///                     ↘ Kicked          (KICK_NOTIFY: terminal, no reconnect)
    ///   any  → Closed        (manual Disconnect(); terminal until ConnectAsync())
    /// </summary>
    public enum ConnStatus
    {
        Idle,
        Connecting,
        Connected,
        WaitingReconnect,
        Closed,
        Kicked,
    }

    public sealed class ChirpClientOptions
    {
        /// <summary>The server has no idle kick; periodic pings keep
        /// intermediaries honest.</summary>
        public int HeartbeatIntervalMs { get; set; } = 25_000;

        /// <summary>Die after this many consecutive pings without any pong.</summary>
        public int MaxMissedPongs { get; set; } = 2;

        public int RequestTimeoutMs { get; set; } = 10_000;

        public int ReconnectBaseMs { get; set; } = 500;

        public int ReconnectMaxMs { get; set; } = 15_000;

        /// <summary>± fraction applied to each reconnect delay.</summary>
        public double JitterRatio { get; set; } = 0.2;
    }

    /// <summary>
    /// Typed websocket client for the Packet protocol: framing,
    /// request/response correlation by sequence, heartbeats, auto reconnect
    /// with backoff and terminal kick handling. A port of the web/mobile
    /// companions' ChirpClient.
    /// </summary>
    /// Split across partials by concern: connection/request core here,
    /// ChirpClient.Chat.cs (feature API) and ChirpClient.Receive.cs
    /// (transport receive path).
    /// </summary>
    public sealed partial class ChirpClient : IDisposable
    {
        public ChirpClient(string url, Func<string, IChirpTransport>? transportFactory = null,
            ChirpClientOptions? options = null)
        {
            _url = url;
            _transportFactory = transportFactory ?? (_ => new ClientWebSocketTransport());
            Options = options ?? new ChirpClientOptions();
            _random = new Random();
        }

        private readonly string _url;
        private readonly Func<string, IChirpTransport> _transportFactory;
        private readonly Random _random;
        private readonly object _gate = new object();

        public ChirpClientOptions Options { get; }

        private IChirpTransport? _transport;
        private FrameDecoder _decoder = new FrameDecoder();
        private readonly Dictionary<long, Pending> _pending = new Dictionary<long, Pending>();

        // MsgID is an enum: identity equality makes the handler map safe.
        private readonly Dictionary<MsgID, List<Action<byte[]>>> _notifyHandlers =
            new Dictionary<MsgID, List<Action<byte[]>>>();
        private readonly List<Action<ConnStatus>> _statusListeners = new List<Action<ConnStatus>>();

        private ConnStatus _status = ConnStatus.Idle;
        private long _seqCounter;
        private int _attempt;
        private int _missedPongs;
        private bool _kicked;
        private bool _disposed;

        private Timer? _pingTimer;
        private CancellationTokenSource? _reconnectCts;

        // Hook registries (sdks/core parity). All uses snapshot under _gate
        // and invoke the user objects outside the lock.
        private IMessageInterceptor? _interceptor;
        private IAuthProvider? _authProvider;
        private IMessageStore? _messageStore;
        private readonly List<IChatEventListener> _eventListeners = new List<IChatEventListener>();
        private readonly List<ICommandHandler> _commands = new List<ICommandHandler>();

        public ConnStatus Status
        {
            get { lock (_gate) return _status; }
        }

        /// <summary>True after the server sent KICK_NOTIFY; auto reconnect stays off.</summary>
        public bool Kicked
        {
            get { lock (_gate) return _kicked; }
        }

        /// <summary>server_time − local_time at the last pong; null before the first.</summary>
        public long? ClockOffsetMs { get; private set; }

        public event Action<ConnStatus> StatusChanged
        {
            add { lock (_gate) _statusListeners.Add(value); }
            remove { lock (_gate) _statusListeners.Remove(value); }
        }

        /// <summary>A reconnect attempt was scheduled: (attempt, delayMs),
        /// attempt starting at 1. Fires from the close path, before the
        /// delay elapses.</summary>
        public event Action<int, int>? Reconnecting;

        /// <summary>An automatic reconnect succeeded and the socket is
        /// Connected again. Note the session is NOT re-established — replay
        /// LOGIN here (the C++ reference SDK's OnReconnected contract).</summary>
        public event Action? Reconnected;

        /// <summary>Subscribe to server pushes (sequence === 0 packets). Unknown
        /// msgIds are ignored, so subscribing to one is purely optional. Returns
        /// the unsubscribe function.</summary>
        public Action OnNotify(MsgID msgId, Action<byte[]> handler)
        {
            lock (_gate)
            {
                if (!_notifyHandlers.TryGetValue(msgId, out var handlers))
                {
                    handlers = new List<Action<byte[]>>();
                    _notifyHandlers[msgId] = handlers;
                }
                handlers.Add(handler);
            }
            return () =>
            {
                lock (_gate)
                {
                    if (_notifyHandlers.TryGetValue(msgId, out var handlers))
                    {
                        handlers.Remove(handler);
                    }
                }
            };
        }

        // ----- hooks (sdks/core parity) -----

        /// <summary>Register the message interceptor (send/receive rewrite
        /// and audit point). Register before ConnectAsync(); replaceable
        /// later, but an in-flight receive may still see the previous
        /// instance.</summary>
        public void SetMessageInterceptor(IMessageInterceptor interceptor)
        {
            lock (_gate) _interceptor = interceptor;
        }

        /// <summary>Register the auth provider used by LoginAsync.</summary>
        public void SetAuthProvider(IAuthProvider provider)
        {
            lock (_gate) _authProvider = provider;
        }

        /// <summary>Register the local archive: every message that passes
        /// the interceptor (sent and received) is saved into it.</summary>
        public void SetMessageStore(IMessageStore store)
        {
            lock (_gate) _messageStore = store;
        }

        /// <summary>Subscribe a lifecycle listener; multiple allowed.</summary>
        public void AddListener(IChatEventListener listener)
        {
            lock (_gate) _eventListeners.Add(listener);
        }

        public bool RemoveListener(IChatEventListener listener)
        {
            lock (_gate) return _eventListeners.Remove(listener);
        }

        /// <summary>Register a '/'-command handler; registration order is
        /// match order. The first registered handler enables local routing.</summary>
        public void RegisterCommand(ICommandHandler handler)
        {
            lock (_gate) _commands.Add(handler);
        }

        public bool UnregisterCommand(ICommandHandler handler)
        {
            lock (_gate) return _commands.Remove(handler);
        }

        private IMessageInterceptor? SnapshotInterceptor()
        {
            lock (_gate) return _interceptor;
        }

        private IAuthProvider? SnapshotProvider()
        {
            lock (_gate) return _authProvider;
        }

        private IMessageStore? SnapshotStore()
        {
            lock (_gate) return _messageStore;
        }

        private void NotifyEventListeners(Action<IChatEventListener> fire)
        {
            IChatEventListener[] listeners;
            lock (_gate)
            {
                listeners = _eventListeners.ToArray();
            }
            foreach (var listener in listeners)
            {
                try
                {
                    fire(listener);
                }
                catch (Exception)
                {
                    // One bad listener must not starve the others.
                }
            }
        }

        /// <summary>Opens the socket. Completes on open, throws if it closed
        /// first. An explicit connect is a user action: kick/terminal state is
        /// cleared and the backoff restarts.</summary>
        public async Task ConnectAsync(CancellationToken ct = default)
        {
            IChirpTransport stale;
            lock (_gate)
            {
                ThrowIfDisposed();
                _kicked = false;
                CancelReconnectTimer();
                stale = _transport!;
                _transport = null;
            }
            if (stale != null)
            {
                stale.Closed -= OnTransportClosed;
                stale.Dispose();
            }

            SetStatus(ConnStatus.Connecting);
            var transport = _transportFactory(_url);
            try
            {
                await transport.OpenAsync(_url, ct).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // The transport was never wired to our events; just drop it.
                transport.Dispose();
                HandleClose();
                throw new RequestError(RequestErrorKind.Closed);
            }
            lock (_gate)
            {
                if (_disposed)
                {
                    transport.Dispose();
                    return;
                }
                _transport = transport;
                _missedPongs = 0;
                _decoder.Reset();
            }
            transport.BinaryMessage += OnTransportMessage;
            transport.Closed += OnTransportClosed;
            SetStatus(ConnStatus.Connected);
            StartHeartbeat();
        }

        /// <summary>Manual close; no reconnect follows.</summary>
        public void Disconnect()
        {
            IChirpTransport? transport;
            lock (_gate)
            {
                transport = _transport;
                _transport = null;
            }
            SetStatus(ConnStatus.Closed);
            ClearHeartbeat();
            RejectAllPending(RequestErrorKind.Closed);
            CancelReconnectTimer();
            if (transport != null)
            {
                transport.BinaryMessage -= OnTransportMessage;
                transport.Closed -= OnTransportClosed;
            }
            if (transport != null)
            {
                var closeTask = transport.CloseAsync();
                closeTask.ContinueWith(_ => transport.Dispose(), TaskScheduler.Default);
            }
        }

        /// <summary>Reset the reconnect backoff after a successful login. Opening
        /// the socket alone proves little (the server may still refuse the
        /// token), so the caller invokes this once the LOGIN round-trip has
        /// succeeded.</summary>
        public void ResetBackoff()
        {
            lock (_gate) _attempt = 0;
        }

        /// <summary>Immediate heartbeat (mobile app lifecycle resume).</summary>
        public void HeartbeatNow()
        {
            if (Status == ConnStatus.Connected) SendPing();
        }

        /// <summary>Typed request/response. Throws RequestError(Closed) when not
        /// connected, (Timeout) on deadline; server error codes are decided by
        /// the caller from the decoded response.</summary>
        public async Task<TResp> RequestAsync<TResp>(MessageSpec<TResp> spec, IMessage request,
            int? timeoutMs = null) where TResp : IMessage<TResp>
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            var body = await RawRequestAsync(
                spec.ReqMsgId, request.ToByteArray(), timeoutMs ?? Options.RequestTimeoutMs)
                .ConfigureAwait(false);
            return spec.Decode(body);
        }

        /// <summary>Fire-and-forget send for messages that never get a response
        /// frame (MESSAGE_ACK, TYPING). Throws RequestError(Closed) when not
        /// connected.</summary>
        public void Send(MsgID msgId, byte[] body)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            RawSend(msgId, body, Interlocked.Increment(ref _seqCounter));
        }

        public void Dispose()
        {
            lock (_gate) _disposed = true;
            Disconnect();
        }

        // ----- internals -----

        private async Task<byte[]> RawRequestAsync(MsgID msgId, byte[] body, int timeoutMs)
        {
            var sequence = Interlocked.Increment(ref _seqCounter);
            var tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            var pending = new Pending(tcs);
            lock (_gate)
            {
                pending.Timeout = new Timer(_ =>
                {
                    lock (_gate) _pending.Remove(sequence);
                    tcs.TrySetException(new RequestError(RequestErrorKind.Timeout));
                }, null, timeoutMs, Timeout.Infinite);
                _pending[sequence] = pending;
            }
            try
            {
                RawSend(msgId, body, sequence);
            }
            catch (Exception error)
            {
                lock (_gate)
                {
                    pending.Timeout?.Dispose();
                    _pending.Remove(sequence);
                }
                tcs.TrySetException(error is RequestError re
                    ? re
                    : new RequestError(RequestErrorKind.Closed));
            }
            return await tcs.Task.ConfigureAwait(false);
        }

        private void RawSend(MsgID msgId, byte[] body, long sequence)
        {
            IChirpTransport transport;
            lock (_gate)
            {
                transport = _transport!;
                if (transport == null || _status != ConnStatus.Connected)
                {
                    throw new RequestError(RequestErrorKind.Closed);
                }
            }
            var packet = new Packet
            {
                MsgId = msgId,
                Sequence = sequence,
                Body = ByteString.CopyFrom(body),
            };
            // SendAsync is sync-over-async on ClientWebSocket; the companions'
            // send path is synchronous too. Failures surface on the next
            // receive or through the Closed event.
            transport.SendAsync(FrameCodec.Encode(packet.ToByteArray()))
                .GetAwaiter().GetResult();
        }

        private void SetStatus(ConnStatus status)
        {
            Action<ConnStatus>[] listeners;
            lock (_gate)
            {
                if (_status == status) return;
                _status = status;
                listeners = _statusListeners.ToArray();
            }
            foreach (var listener in listeners)
            {
                SafeInvoke(listener, status);
            }
            NotifyEventListeners(l => l.OnConnectionStateChanged(status));
        }

        private static void SafeInvoke(Action<ConnStatus> listener, ConnStatus status)
        {
            try
            {
                listener(status);
            }
            catch (Exception)
            {
                // One bad listener must not starve the others.
            }
        }

        private void StartHeartbeat()
        {
            ClearHeartbeat();
            _pingTimer = new Timer(_ => SendPing(), null,
                Options.HeartbeatIntervalMs, Options.HeartbeatIntervalMs);
        }

        private void ClearHeartbeat()
        {
            _pingTimer?.Dispose();
            _pingTimer = null;
        }

        private void SendPing()
        {
            if (_missedPongs >= Options.MaxMissedPongs)
            {
                // Consecutive missed pongs: the link is dead in practice —
                // close it and let the reconnect path take over.
                var transport = _transport;
                if (transport != null)
                {
                    var closeTask = transport.CloseAsync();
                    closeTask.ContinueWith(_ => transport.Dispose(), TaskScheduler.Default);
                }
                return;
            }
            _missedPongs++;
            var ping = new HeartbeatPing { Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() };
            try
            {
                RawSend(MsgID.HeartbeatPing, ping.ToByteArray(), Interlocked.Increment(ref _seqCounter));
            }
            catch (Exception)
            {
                // Socket died underneath us; the Closed event will fire.
            }
        }

        private void OnTransportClosed()
        {
            IChirpTransport? transport;
            lock (_gate)
            {
                transport = _transport;
                _transport = null;
            }
            if (transport != null)
            {
                transport.BinaryMessage -= OnTransportMessage;
                transport.Closed -= OnTransportClosed;
            }
            ClearHeartbeat();
            RejectAllPending(RequestErrorKind.Closed);
            bool kicked;
            lock (_gate)
            {
                _decoder.Reset();
                kicked = _kicked;
            }
            if (kicked)
            {
                SetStatus(ConnStatus.Kicked);
                return;
            }
            ConnStatus current = Status;
            if (current == ConnStatus.Closed) return; // manual Disconnect()
            SetStatus(ConnStatus.WaitingReconnect);
            ScheduleReconnect();
        }

        private void HandleClose()
        {
            // Open-failure path: the transport was never wired to events.
            ClearHeartbeat();
            RejectAllPending(RequestErrorKind.Closed);
            bool kicked;
            lock (_gate)
            {
                _decoder.Reset();
                kicked = _kicked;
            }
            if (kicked)
            {
                SetStatus(ConnStatus.Kicked);
                return;
            }
            if (Status == ConnStatus.Closed) return;
            SetStatus(ConnStatus.WaitingReconnect);
            ScheduleReconnect();
        }

        private void RejectAllPending(RequestErrorKind kind)
        {
            Pending[] entries;
            lock (_gate)
            {
                entries = new Pending[_pending.Values.Count];
                _pending.Values.CopyTo(entries, 0);
                _pending.Clear();
            }
            foreach (var entry in entries)
            {
                entry.Timeout?.Dispose();
                entry.Completion.TrySetException(new RequestError(kind));
            }
        }

        private void ScheduleReconnect()
        {
            int delay;
            int attempt;
            lock (_gate)
            {
                var baseMs = Math.Min(
                    Options.ReconnectBaseMs * (int)Math.Pow(2, Math.Min(_attempt, 20)),
                    Options.ReconnectMaxMs);
                var jitter = (int)Math.Round(baseMs * Options.JitterRatio);
                delay = baseMs + _random.Next(jitter * 2 + 1) - jitter;
                attempt = ++_attempt;
            }
            Reconnecting?.Invoke(attempt, delay);
            NotifyEventListeners(l => l.OnReconnecting(attempt, delay));
            _reconnectCts = new CancellationTokenSource();
            var token = _reconnectCts.Token;
            _ = Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(delay, token).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
                // A failed attempt loops straight back through HandleClose.
                try
                {
                    await ConnectAsync().ConfigureAwait(false);
                }
                catch (Exception)
                {
                    return;
                }
                // Connected again only if nothing tore the client down while
                // the socket was opening — dispose returns silently with the
                // status stuck in Connecting, and a kick flips to Kicked.
                if (Status == ConnStatus.Connected)
                {
                    Reconnected?.Invoke();
                    NotifyEventListeners(l => l.OnReconnected());
                }
            }, token);
        }

        private void CancelReconnectTimer()
        {
            _reconnectCts?.Cancel();
            _reconnectCts?.Dispose();
            _reconnectCts = null;
        }

        private void ClearHeartbeatLocked()
        {
            _pingTimer?.Dispose();
            _pingTimer = null;
        }

        private void ThrowIfDisposed()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(ChirpClient));
        }

        private sealed class Pending
        {
            public Pending(TaskCompletionSource<byte[]> completion)
            {
                Completion = completion;
            }

            public TaskCompletionSource<byte[]> Completion { get; }
            public Timer? Timeout { get; set; }
        }
    }
}
