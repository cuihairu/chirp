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
    public sealed class ChirpClient : IDisposable
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

        /// <summary>Recent messages from the local archive, newest first;
        /// empty without a store.</summary>
        public List<Chirp.Chat.ChatMessage> LoadHistory(Chirp.Chat.ChannelType type,
            string channelId, int limit, long beforeTimestamp = 0)
        {
            var store = SnapshotStore();
            return store == null
                ? new List<Chirp.Chat.ChatMessage>()
                : store.Load(type, channelId, limit, beforeTimestamp);
        }

        /// <summary>Advance the read cursor on the local archive; no-op
        /// without a store (the memory store does not track read state).</summary>
        public void MarkRead(Chirp.Chat.ChannelType type, string channelId, string messageId)
        {
            SnapshotStore()?.MarkRead(type, channelId, messageId);
        }

        /// <summary>Unread count from the local archive; 0 without a store.</summary>
        public int GetUnreadCount(Chirp.Chat.ChannelType type, string channelId)
        {
            return SnapshotStore()?.GetUnreadCount(type, channelId) ?? 0;
        }

        /// <summary>Drop archived messages older than the cutoff; no-op
        /// without a store.</summary>
        public void CleanupMessages(long olderThanTimestamp)
        {
            SnapshotStore()?.Cleanup(olderThanTimestamp);
        }

        /// <summary>LOGIN round-trip (协议层便捷方法). Token resolution:
        /// explicit <paramref name="token"/> → AuthProvider.GetToken() →
        /// userId (scaffold gateways take the userId as the token). On
        /// AUTH_FAILED with a provider registered, RenewTokenAsync gets
        /// exactly one shot and the login is retried once with the fresh
        /// token. Terminal outcomes fire listener.OnLoginResult and
        /// provider.OnAuthResult; transport errors (RequestError
        /// Closed/Timeout) surface directly and are not auth results.
        /// Throws RequestError(Server) on a non-OK code; resets the
        /// reconnect backoff on success.</summary>
        public async Task<Chirp.Auth.LoginResponse> LoginAsync(string userId, string deviceId,
            string? token = null, int? timeoutMs = null)
        {
            var provider = SnapshotProvider();
            var issued = !string.IsNullOrEmpty(token) ? token
                : provider != null ? provider.GetToken()
                : userId;
            var resp = await PostLoginAsync(issued, deviceId, timeoutMs).ConfigureAwait(false);
            if (resp.Code == Chirp.Common.ErrorCode.AuthFailed && provider != null)
            {
                string? renewed = null;
                try
                {
                    renewed = await provider.RenewTokenAsync().ConfigureAwait(false);
                }
                catch (Exception)
                {
                    renewed = null; // a throwing provider means "no renewal"
                }
                if (!string.IsNullOrEmpty(renewed))
                {
                    resp = await PostLoginAsync(renewed!, deviceId, timeoutMs).ConfigureAwait(false);
                }
            }
            NotifyEventListeners(l => l.OnLoginResult(resp.Code, userId));
            try
            {
                provider?.OnAuthResult(resp.Code, userId);
            }
            catch (Exception)
            {
                // One bad hook must not mask the login outcome.
            }
            if (resp.Code != Chirp.Common.ErrorCode.Ok)
            {
                throw new RequestError(RequestErrorKind.Server, resp.Code);
            }
            ResetBackoff();
            return resp;
        }

        /// <summary>Chat send through the full pipeline (对齐 C++ 参考实现的
        /// SendMessage):validation → '/'-command routing → OnBeforeSend
        /// (rewrite or block) → local archive → wire → OnAfterSend. Private
        /// sends normalize channel_id to the sorted "a|b" pair; every other
        /// channel type needs an explicit ChannelId. Messages that never go
        /// on the wire throw RequestError(Blocked) (command handled, unknown
        /// command, interceptor drop); invalid arguments throw
        /// ArgumentException; transport errors surface as
        /// RequestError(Closed/Timeout). A returned response may still carry
        /// a non-OK Code (rate limit, invalid param…) — read resp.Code.</summary>
        public async Task<Chirp.Chat.SendMessageResponse> SendMessageAsync(SendOptions options,
            string content, string senderId, int? timeoutMs = null)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }
            if (Status != ConnStatus.Connected)
            {
                // 状态检查先于参数校验(C++ 参考实现顺序)。
                throw new RequestError(RequestErrorKind.Closed);
            }
            if (string.IsNullOrEmpty(content))
            {
                throw new ArgumentException("content is empty", nameof(content));
            }
            if (senderId == null)
            {
                throw new ArgumentException("senderId is required", nameof(senderId));
            }
            if (options.ChannelType == Chirp.Chat.ChannelType.Private)
            {
                if (string.IsNullOrEmpty(options.ReceiverId))
                {
                    throw new ArgumentException("private send needs ReceiverId", nameof(options));
                }
            }
            else if (string.IsNullOrEmpty(options.ChannelId))
            {
                throw new ArgumentException(
                    options.ChannelType + " send needs an explicit ChannelId", nameof(options));
            }

            if (content.StartsWith("/", StringComparison.Ordinal) &&
                TryRouteCommand(content, senderId, out var handled))
            {
                throw new RequestError(RequestErrorKind.Blocked, message: handled
                    ? "command handled locally"
                    : "unknown command, dropped locally: " + content);
            }

            var request = BuildSendRequest(options, content, senderId);
            var interceptor = SnapshotInterceptor();
            if (interceptor != null)
            {
                bool allowed;
                try
                {
                    allowed = interceptor.OnBeforeSend(request);
                }
                catch (Exception)
                {
                    allowed = false; // a throwing interceptor is a blocking one
                }
                if (!allowed)
                {
                    throw new RequestError(RequestErrorKind.Blocked,
                        message: "message blocked by interceptor");
                }
            }
            var store = SnapshotStore();
            if (store != null)
            {
                try
                {
                    store.Save(StoredCopyOf(request));
                }
                catch (Exception)
                {
                    // A failing store must not take the send down with it.
                }
            }
            var resp = await RequestAsync(Specs.SendMessage, request, timeoutMs).ConfigureAwait(false);
            if (interceptor != null)
            {
                try
                {
                    interceptor.OnAfterSend(request);
                }
                catch (Exception)
                {
                    // One bad hook must not mask the response.
                }
            }
            return resp;
        }

        // ----- hook internals -----

        private Task<Chirp.Auth.LoginResponse> PostLoginAsync(string token, string deviceId,
            int? timeoutMs)
        {
            return RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest
            {
                Token = token,
                DeviceId = deviceId,
                Platform = "unity",
                SupportsMessageAck = true,
            }, timeoutMs);
        }

        private static Chirp.Chat.SendMessageRequest BuildSendRequest(SendOptions options,
            string content, string senderId)
        {
            string channelId;
            if (options.ChannelType == Chirp.Chat.ChannelType.Private)
            {
                // 私聊归一化:双方 id 字典序小者在前(服务端同款)。
                channelId = string.CompareOrdinal(senderId, options.ReceiverId) <= 0
                    ? senderId + "|" + options.ReceiverId
                    : options.ReceiverId + "|" + senderId;
            }
            else
            {
                channelId = options.ChannelId;
            }
            return new Chirp.Chat.SendMessageRequest
            {
                SenderId = senderId,
                ReceiverId = options.ReceiverId,
                ChannelType = options.ChannelType,
                ChannelId = channelId,
                MsgType = options.MsgType,
                ReplyToMessageId = options.ReplyToMessageId,
                Content = Google.Protobuf.ByteString.CopyFrom(
                    Encoding.UTF8.GetBytes(content)),
                ClientTimestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            };
        }

        /// <summary>发送侧存档:字段与线上请求等价,MessageId 留空(fire-and-
        /// forget 拿不到服务端 id;要 id 读 resp.MessageId——C++ 参考实现行为)。</summary>
        private static Chirp.Chat.ChatMessage StoredCopyOf(Chirp.Chat.SendMessageRequest request)
        {
            return new Chirp.Chat.ChatMessage
            {
                SenderId = request.SenderId,
                ReceiverId = request.ReceiverId,
                ChannelType = request.ChannelType,
                ChannelId = request.ChannelId,
                MsgType = request.MsgType,
                Content = request.Content,
                Timestamp = request.ClientTimestamp,
                ReplyToMessageId = request.ReplyToMessageId,
            };
        }

        /// <summary>'/'-command routing (C++ 契约):zero handlers → pass
        /// through (returns false). With any handler registered the message
        /// is consumed locally either way — returns true with
        /// <paramref name="handled"/> telling hit from miss.</summary>
        private bool TryRouteCommand(string content, string senderId, out bool handled)
        {
            handled = false;
            ICommandHandler[] commands;
            lock (_gate)
            {
                commands = _commands.ToArray();
            }
            if (commands.Length == 0)
            {
                return false;
            }
            var name = content.Substring(1);
            var args = "";
            var space = name.IndexOf(' ');
            if (space >= 0)
            {
                args = name.Substring(space + 1);
                name = name.Substring(0, space);
            }
            foreach (var command in commands)
            {
                if (command.Name != name) continue;
                try
                {
                    handled = command.Execute(args, senderId);
                }
                catch (Exception)
                {
                    handled = false; // a throwing handler declines
                }
                if (handled) return true;
            }
            return true;
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

        private void OnTransportMessage(byte[] message)
        {
            IReadOnlyList<byte[]> frames;
            try
            {
                lock (_gate)
                {
                    frames = _decoder.Feed(message);
                }
            }
            catch (FrameError)
            {
                // A corrupted stream cannot be resynchronized; treat it as a
                // dead link.
                var transport = _transport;
                if (transport != null)
                {
                    transport.CloseAsync().ContinueWith(_ => transport.Dispose(), TaskScheduler.Default);
                }
                return;
            }
            foreach (var frame in frames)
            {
                HandleFrame(frame);
            }
        }

        private void HandleFrame(byte[] frame)
        {
            Packet packet;
            try
            {
                packet = Packet.Parser.ParseFrom(frame);
            }
            catch (Exception)
            {
                return; // undecodable packet: ignore rather than kill the connection
            }
            if (packet.Sequence == 0)
            {
                DispatchNotify(packet.MsgId, packet.Body.ToByteArray());
                return;
            }
            if (packet.MsgId == MsgID.HeartbeatPong)
            {
                // The pong echoes the ping's sequence and is not in the
                // pending table.
                OnPong(packet.Body.ToByteArray());
                return;
            }
            Pending? entry;
            lock (_gate)
            {
                if (!_pending.Remove(packet.Sequence, out entry)) return;
            }
            entry.Timeout?.Dispose();
            entry.Completion.TrySetResult(packet.Body.ToByteArray());
        }

        private void OnPong(byte[] body)
        {
            _missedPongs = 0;
            try
            {
                var pong = HeartbeatPong.Parser.ParseFrom(body);
                ClockOffsetMs = pong.ServerTime - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
            catch (Exception)
            {
                // A malformed pong still proves the link is alive.
            }
        }

        private void DispatchNotify(MsgID msgId, byte[] body)
        {
            if (msgId == MsgID.KickNotify)
            {
                MarkKicked();
                string reason;
                try
                {
                    reason = Chirp.Auth.KickNotify.Parser.ParseFrom(body).Reason;
                }
                catch (Exception)
                {
                    reason = "";
                }
                NotifyEventListeners(l => l.OnKicked(reason));
                return;
            }
            if (msgId == MsgID.ChatMessageNotify && HasChatReceivePipeline())
            {
                // C++ 对齐:拦截/存档/监听管线接管。parse 失败落回原始分发,
                // 行为与零钩子注册完全一致。
                if (RunChatReceivePipeline(msgId, body)) return;
            }
            DispatchRawNotify(msgId, body);
        }

        private void DispatchRawNotify(MsgID msgId, byte[] body)
        {
            Action<byte[]>[] handlers;
            lock (_gate)
            {
                if (!_notifyHandlers.TryGetValue(msgId, out var list)) return;
                handlers = list.ToArray();
            }
            foreach (var handler in handlers)
            {
                try
                {
                    handler(body);
                }
                catch (Exception)
                {
                    // One bad handler must not starve the others.
                }
            }
        }

        private bool HasChatReceivePipeline()
        {
            lock (_gate)
            {
                return _interceptor != null || _messageStore != null || _eventListeners.Count > 0;
            }
        }

        /// <summary>接收管线(对齐 C++ HandleChatNotify):OnBeforeReceive
        /// (false = 全丢:不存档、不触发、不分发)→ 存档 → OnAfterReceive →
        /// listeners.OnMessageReceived → 原始 body 分发给 OnNotify 订阅者
        /// (拦截器改写只影响解析后的消息,原始帧不变——manager 的
        /// OnChatMessage 收到的仍是线上原文)。返回 false = parse 失败。</summary>
        private bool RunChatReceivePipeline(MsgID msgId, byte[] body)
        {
            Chirp.Chat.ChatMessage message;
            try
            {
                message = Chirp.Chat.ChatMessage.Parser.ParseFrom(body);
            }
            catch (Exception)
            {
                return false;
            }
            IMessageInterceptor? interceptor;
            IMessageStore? store;
            lock (_gate)
            {
                interceptor = _interceptor;
                store = _messageStore;
            }
            if (interceptor != null)
            {
                bool allowed;
                try
                {
                    allowed = interceptor.OnBeforeReceive(message);
                }
                catch (Exception)
                {
                    allowed = false; // a throwing interceptor is a blocking one
                }
                if (!allowed) return true;
            }
            if (store != null)
            {
                try
                {
                    store.Save(message);
                }
                catch (Exception)
                {
                    // A failing store must not take the receive loop down.
                }
            }
            if (interceptor != null)
            {
                try
                {
                    interceptor.OnAfterReceive(message);
                }
                catch (Exception)
                {
                    // One bad hook must not break the pipeline.
                }
            }
            NotifyEventListeners(l => l.OnMessageReceived(message));
            DispatchRawNotify(msgId, body);
            return true;
        }

        private void MarkKicked()
        {
            IChirpTransport? transport;
            lock (_gate)
            {
                // KICK then reconnect would just fight the new device, so auto
                // reconnect stays off and the game is told to return to login.
                // The status flip happens HERE, not via the close event: the
                // subscription teardown below runs before the transport
                // announces the close.
                _kicked = true;
                ClearHeartbeatLocked();
                CancelReconnectTimer();
                transport = _transport;
                _transport = null;
            }
            RejectAllPending(RequestErrorKind.Kicked);
            SetStatus(ConnStatus.Kicked);
            if (transport != null)
            {
                transport.BinaryMessage -= OnTransportMessage;
                transport.Closed -= OnTransportClosed;
                transport.CloseAsync().ContinueWith(_ => transport.Dispose(), TaskScheduler.Default);
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
