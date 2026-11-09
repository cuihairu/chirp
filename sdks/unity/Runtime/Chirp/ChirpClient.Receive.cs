using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Gateway;

namespace Chirp.Sdk
{
    /// <summary>Receive path of <see cref="ChirpClient"/> (partial): transport frame handling, notify dispatch and the chat receive pipeline. Pure code motion from ChirpClient.cs; all state stays on the core partial.</summary>
    public sealed partial class ChirpClient
    {
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
            if (msgId == MsgID.GroupMemberAliasUpdatedNotify)
            {
                Chirp.Chat.GroupMemberAliasUpdatedNotify notify;
                try
                {
                    notify = Chirp.Chat.GroupMemberAliasUpdatedNotify.Parser.ParseFrom(body);
                }
                catch (Exception)
                {
                    return; // malformed notify: ignore
                }
                NotifyEventListeners(l => l.OnGroupMemberAliasUpdated(notify.GroupId, notify.UserId, notify.Alias));
                return;
            }
            if (msgId == MsgID.GroupMemberMutedNotify)
            {
                Chirp.Chat.GroupMemberMutedNotify notify;
                try
                {
                    notify = Chirp.Chat.GroupMemberMutedNotify.Parser.ParseFrom(body);
                }
                catch (Exception)
                {
                    return; // malformed notify: ignore
                }
                NotifyEventListeners(l => l.OnGroupMemberMutedUpdated(
                    notify.GroupId, notify.UserId, notify.MutedUntilTs, notify.OperatorId));
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
    }
}
