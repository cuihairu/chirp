using System;
using System.Collections.Concurrent;
using System.Text;
using System.Threading.Tasks;
using Chirp.Gateway;
using UnityEngine;

namespace Chirp.Sdk
{
    /// <summary>
    /// Thin Unity entry point over <see cref="ChirpClient"/>. ChirpClient's
    /// callbacks arrive on its receive-loop thread; Unity's API is main-thread
    /// only, so this component queues them and replays them in Update().
    /// Games subscribe to the On* events; nothing else here is required.
    ///
    /// This manager owns the chat-plane connection only. Party (7501), voice
    /// (9001) and social (8001) are separate services: open additional
    /// connections with <see cref="CreateClient"/> and pump their callbacks
    /// through <see cref="RunOnMainThread"/> — see README.md.
    /// </summary>
    public sealed class ChirpManager : MonoBehaviour
    {
        [Tooltip("ws://host:port of the chat plane (chat 7001 in the dev topology)")]
        public string ChatUrl = "ws://127.0.0.1:7001";

        [Tooltip("Replay the last LOGIN automatically after an automatic "
            + "reconnect succeeds (a fresh socket carries no session).")]
        public bool AutoRelogin = false;

        private ChirpClient? _client;
        private readonly ConcurrentQueue<Action> _mainThread = new ConcurrentQueue<Action>();
        private string? _reloginUserId;
        private string? _reloginDeviceId;
        private string? _reloginToken;

        /// <summary>Connection state changes, replayed on the main thread.</summary>
        public event Action<ConnStatus>? OnStatusChanged;

        /// <summary>The device was kicked (another login took the session).</summary>
        public event Action<string>? OnKicked;

        /// <summary>Live chat pushes, replayed on the main thread.</summary>
        public event Action<Chirp.Chat.ChatMessage>? OnChatMessage;

        /// <summary>Read receipts for messages this user sent.</summary>
        public event Action<Chirp.Chat.MessageReadNotify>? OnMessageRead;

        /// <summary>Peer started/stopped typing in a channel.</summary>
        public event Action<Chirp.Chat.TypingIndicator>? OnTypingIndicator;

        /// <summary>A reaction was added to a message.</summary>
        public event Action<Chirp.Chat.ReactionAddedNotify>? OnReactionAdded;

        /// <summary>A reaction was removed from a message.</summary>
        public event Action<Chirp.Chat.ReactionRemovedNotify>? OnReactionRemoved;

        /// <summary>A message was edited in place.</summary>
        public event Action<Chirp.Chat.MessageEditedNotify>? OnMessageEdited;

        /// <summary>A message was deleted (tombstone the local copy).</summary>
        public event Action<Chirp.Chat.MessageDeletedNotify>? OnMessageDeleted;

        /// <summary>AutoRelogin: the LOGIN replay after a reconnect failed
        /// (expired token, auth rejection). The connection stays up but the
        /// game must surface a re-login prompt or refresh the token.</summary>
        public event Action<Exception>? OnReloginFailed;

        public ChirpClient? Client => _client;
        public bool IsConnected => _client != null && _client.Status == ConnStatus.Connected;
        public bool IsKicked => _client != null && _client.Kicked;

        private void Update()
        {
            while (_mainThread.TryDequeue(out var action))
            {
                action();
            }
        }

        private void OnDestroy() => Disconnect();

        private void OnApplicationQuit() => Disconnect();

        /// <summary>Opens the chat websocket. Safe to call again after a
        /// disconnect or a kick.</summary>
        public Task ConnectAsync()
        {
            if (_client == null)
            {
                _client = new ChirpClient(ChatUrl);
                _client.StatusChanged += s => _mainThread.Enqueue(() => OnStatusChanged?.Invoke(s));
                _client.Reconnected += () => _mainThread.Enqueue(ReloginAfterReconnect);
                SubscribeNotify(MsgID.ChatMessageNotify, OnChatMessage, Chirp.Chat.ChatMessage.Parser);
                SubscribeNotify(MsgID.MessageReadNotify, OnMessageRead, Chirp.Chat.MessageReadNotify.Parser);
                SubscribeNotify(MsgID.TypingIndicatorNotify, OnTypingIndicator, Chirp.Chat.TypingIndicator.Parser);
                SubscribeNotify(MsgID.ReactionAddedNotify, OnReactionAdded, Chirp.Chat.ReactionAddedNotify.Parser);
                SubscribeNotify(MsgID.ReactionRemovedNotify, OnReactionRemoved, Chirp.Chat.ReactionRemovedNotify.Parser);
                SubscribeNotify(MsgID.MessageEditedNotify, OnMessageEdited, Chirp.Chat.MessageEditedNotify.Parser);
                SubscribeNotify(MsgID.MessageDeletedNotify, OnMessageDeleted, Chirp.Chat.MessageDeletedNotify.Parser);
                _client.OnNotify(MsgID.KickNotify, body =>
                {
                    string reason;
                    try
                    {
                        reason = Chirp.Auth.KickNotify.Parser.ParseFrom(body).Reason;
                    }
                    catch (Exception)
                    {
                        reason = "";
                    }
                    _mainThread.Enqueue(() => OnKicked?.Invoke(reason));
                });
            }
            return _client.ConnectAsync();
        }

        /// <summary>A second connection to another plane's websocket (party
        /// 7501, voice 9001, social 8001). The client is returned unconnected
        /// and unwired: call its ConnectAsync, subscribe its events through
        /// <see cref="RunOnMainThread"/>, and Dispose it when done. Its
        /// lifecycle is the caller's, not the manager's.</summary>
        public ChirpClient CreateClient(string url) => new ChirpClient(url);

        /// <summary>Queue a callback for the main thread. Use this to bridge
        /// events of secondary connections created via <see cref="CreateClient"/>
        /// (the manager's own On* events are already marshalled).</summary>
        public void RunOnMainThread(Action action) => _mainThread.Enqueue(action);

        public void Disconnect() => _client?.Disconnect();

        /// <summary>LOGIN round-trip. Throws <see cref="RequestError"/>; on
        /// AuthFailed the game should show its own login error UI. Scaffold
        /// gateways take the userId as the token; pass <paramref name="token"/>
        /// explicitly against an enhanced auth (JWT) edge. Delegates to
        /// <see cref="ChirpClient.LoginAsync"/>, so a registered IAuthProvider
        /// supplies the token when <paramref name="token"/> is omitted and
        /// gets one renewal chance on AUTH_FAILED.</summary>
        public async Task<Chirp.Auth.LoginResponse> LoginAsync(string userId, string deviceId,
            string? token = null)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            var resp = await client.LoginAsync(userId, deviceId, token).ConfigureAwait(false);
            if (AutoRelogin)
            {
                _reloginUserId = userId;
                _reloginDeviceId = deviceId;
                _reloginToken = token;
            }
            return resp;
        }

        private void ReloginAfterReconnect()
        {
            // Main thread. A fresh socket carries no session; AutoRelogin
            // replays the last successful LOGIN. Failures leave the socket up
            // — the game decides whether to refresh the token or show login.
            if (!AutoRelogin || _reloginUserId == null || _reloginDeviceId == null)
            {
                return;
            }
            _ = ReloginAsync();
        }

        private async Task ReloginAsync()
        {
            try
            {
                await LoginAsync(_reloginUserId!, _reloginDeviceId!, _reloginToken).ConfigureAwait(false);
            }
            catch (Exception e)
            {
                OnReloginFailed?.Invoke(e);
            }
        }

        public async Task LogoutAsync(string userId)
        {
            var client = _client;
            if (client == null) return;
            try
            {
                await client.RequestAsync(Specs.Logout, new Chirp.Auth.LogoutRequest { UserId = userId })
                    .ConfigureAwait(false);
            }
            catch (RequestError)
            {
                // The disconnect below is what matters.
            }
            Disconnect();
        }

        /// <summary>Fire-and-forget chat send (SEND_MESSAGE has a RESP; await
        /// RequestAsync(Specs.SendMessage, ...) when you need the server
        /// message id). Goes straight to the wire: interceptors and '/'-command
        /// routing only apply to <see cref="SendChatMessageAsync"/>.</summary>
        public void SendChatMessage(string senderId, string receiverId, string channelId,
            Chirp.Chat.ChannelType channelType, string text, string replyToMessageId = "")
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            client.Send(MsgID.SendMessageReq, new Chirp.Chat.SendMessageRequest
            {
                SenderId = senderId,
                ReceiverId = receiverId,
                ChannelType = channelType,
                ChannelId = channelId,
                MsgType = Chirp.Chat.MsgType.Text,
                Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(text)),
                ClientTimestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                ReplyToMessageId = replyToMessageId,
            }.ToByteArray());
        }

        /// <summary>Chat send through the full pipeline (interceptor,
        /// '/'-command routing, local archive — see
        /// <see cref="ChirpClient.SendMessageAsync"/>). Returns the server
        /// response (read <c>resp.MessageId</c> for the server id). Private
        /// sends normalize the channel id; a locally blocked message throws
        /// RequestError(Blocked).</summary>
        public Task<Chirp.Chat.SendMessageResponse> SendChatMessageAsync(SendOptions options,
            string senderId, string text, int? timeoutMs = null)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            return client.SendMessageAsync(options, text, senderId, timeoutMs);
        }

        /// <summary>Recent history for a channel, newest first.</summary>
        public Task<Chirp.Chat.GetHistoryResponse> GetHistoryAsync(string userId,
            Chirp.Chat.ChannelType channelType, string channelId, int limit = 50,
            long beforeTimestamp = 0)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            return client.RequestAsync(Specs.GetHistory, new Chirp.Chat.GetHistoryRequest
            {
                UserId = userId,
                ChannelType = channelType,
                ChannelId = channelId,
                Limit = limit,
                BeforeTimestamp = beforeTimestamp,
            });
        }

        /// <summary>Advance the read cursor: clears the unread counter and
        /// tells the other side the message was read.</summary>
        public Task<Chirp.Chat.MarkReadResponse> MarkReadAsync(string userId,
            Chirp.Chat.ChannelType channelType, string channelId, string messageId)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            return client.RequestAsync(Specs.MarkRead, new Chirp.Chat.MarkReadRequest
            {
                UserId = userId,
                ChannelType = channelType,
                ChannelId = channelId,
                MessageId = messageId,
            });
        }

        /// <summary>Fire-and-forget typing hint; send is_typing=false when the
        /// input box empties or the message goes out.</summary>
        public void SendTypingIndicator(string userId, string username,
            Chirp.Chat.ChannelType channelType, string channelId, bool isTyping = true)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            client.Send(MsgID.TypingIndicatorNotify, new Chirp.Chat.TypingIndicator
            {
                UserId = userId,
                Username = username,
                ChannelType = channelType,
                ChannelId = channelId,
                IsTyping = isTyping,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            }.ToByteArray());
        }

        /// <summary>supportsMessageAck=true: every live push must be acked or
        /// the server rolls the delivery back into the offline queue.</summary>
        public void AckChatMessage(string messageId, string userId)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            client.Send(MsgID.MessageAck, new Chirp.Gateway.MessageAck
            {
                MessageId = messageId,
                UserId = userId,
                ReceivedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            }.ToByteArray());
        }

        private void SubscribeNotify<T>(MsgID msgId, Action<T> fire,
            Google.Protobuf.MessageParser<T> parser)
            where T : Google.Protobuf.IMessage<T>
        {
            var client = _client!;
            client.OnNotify(msgId, body =>
            {
                T message;
                try
                {
                    message = parser.ParseFrom(body);
                }
                catch (Exception)
                {
                    return;
                }
                _mainThread.Enqueue(() => fire(message));
            });
        }
    }
}
