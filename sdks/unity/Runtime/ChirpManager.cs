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
    /// </summary>
    public sealed class ChirpManager : MonoBehaviour
    {
        [Tooltip("ws://host:port of the chat plane (chat 7001 in the dev topology)")]
        public string ChatUrl = "ws://127.0.0.1:7001";

        private ChirpClient? _client;
        private readonly ConcurrentQueue<Action> _mainThread = new ConcurrentQueue<Action>();

        /// <summary>Connection state changes, replayed on the main thread.</summary>
        public event Action<ConnStatus>? OnStatusChanged;

        /// <summary>The device was kicked (another login took the session).</summary>
        public event Action<string>? OnKicked;

        /// <summary>Live chat pushes, replayed on the main thread.</summary>
        public event Action<Chirp.Chat.ChatMessage>? OnChatMessage;

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
                _client.OnNotify(MsgID.ChatMessageNotify, body =>
                {
                    Chirp.Chat.ChatMessage message;
                    try
                    {
                        message = Chirp.Chat.ChatMessage.Parser.ParseFrom(body);
                    }
                    catch (Exception)
                    {
                        return;
                    }
                    _mainThread.Enqueue(() => OnChatMessage?.Invoke(message));
                });
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

        public void Disconnect() => _client?.Disconnect();

        /// <summary>LOGIN round-trip. Throws <see cref="RequestError"/>; on
        /// AuthFailed the game should show its own login error UI.</summary>
        public async Task<Chirp.Auth.LoginResponse> LoginAsync(string userId, string deviceId)
        {
            var client = _client ?? throw new InvalidOperationException("ConnectAsync() first");
            var resp = await client.RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest
            {
                Token = userId,
                DeviceId = deviceId,
                Platform = "unity",
                SupportsMessageAck = true,
            }).ConfigureAwait(false);
            if (resp.Code != Chirp.Common.ErrorCode.Ok)
            {
                throw new RequestError(RequestErrorKind.Server, resp.Code);
            }
            client.ResetBackoff();
            return resp;
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
        /// message id).</summary>
        public void SendChatMessage(string senderId, string receiverId, string channelId,
            Chirp.Chat.ChannelType channelType, string text)
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
    }
}
