using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Text;
using Chirp.Gateway;

namespace Chirp.Sdk
{
    /// <summary>Chat feature surface of <see cref="ChirpClient"/> (partial): login, send, history reads and moderation. Pure code motion from ChirpClient.cs; all state stays on the core partial.</summary>
    public sealed partial class ChirpClient
    {
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
        /// reconnect backoff on success. <paramref name="level"/> is the
        /// client-declared game level for the world-channel gate
        /// (game_chat_features P1); 0 = undeclared.</summary>
        public async Task<Chirp.Auth.LoginResponse> LoginAsync(string userId, string deviceId,
            string? token = null, int? timeoutMs = null, int level = 0)
        {
            var provider = SnapshotProvider();
            var issued = !string.IsNullOrEmpty(token) ? token
                : provider != null ? provider.GetToken()
                : userId;
            var resp = await PostLoginAsync(issued, deviceId, timeoutMs, level).ConfigureAwait(false);
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
                    resp = await PostLoginAsync(renewed!, deviceId, timeoutMs, level).ConfigureAwait(false);
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

        /// <summary>消息搜索(SEARCH_MESSAGE_REQ/RESP 2248/2249):keyword 必填,
        /// 其余可选。返回 SearchMessageResponse{Code, Matches[], HasMore}。
        /// 与 C++ 参考实现对齐:连接态检查先于参数校验,keyword 为空抛
        /// ArgumentException;非连接态抛 RequestError(Closed)。</summary>
        public async Task<Chirp.Chat.SearchMessageResponse> SearchMessagesAsync(string keyword,
            string channelId = "", int[]? contentTypes = null, long beforeTimestamp = 0,
            string beforeMessageId = "", int limit = 20, int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            if (string.IsNullOrEmpty(keyword))
            {
                throw new ArgumentException("keyword is required", nameof(keyword));
            }
            var request = new Chirp.Chat.SearchMessageRequest
            {
                Keyword = keyword,
                ChannelId = channelId ?? "",
                BeforeTimestamp = beforeTimestamp,
                BeforeMessageId = beforeMessageId ?? "",
                Limit = limit > 0 ? limit : 20,
            };
            if (contentTypes != null && contentTypes.Length > 0)
            {
                request.ContentTypes.Add(contentTypes);
            }
            return await RequestAsync(Specs.SearchMessage, request, timeoutMs).ConfigureAwait(false);
        }

        /// <summary>设置群昵称(SET_MEMBER_ALIAS_REQ/RESP 2122/2123):operator
        /// 设置 target 在 group 内的显示别名。alias 空串 = 清除。返回
        /// SetMemberAliasResponse。连接态检查先于参数校验,非连接态抛
        /// RequestError(Closed)。</summary>
        public async Task<Chirp.Chat.SetMemberAliasResponse> SetMemberAliasAsync(string groupId,
            string targetUserId, string alias, int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            var request = new Chirp.Chat.SetMemberAliasRequest
            {
                GroupId = groupId ?? "",
                TargetUserId = targetUserId ?? "",
                Alias = alias ?? "",
            };
            return await RequestAsync(Specs.SetMemberAlias, request, timeoutMs).ConfigureAwait(false);
        }

        /// <summary>群管理员禁言(SET_GROUP_MUTE_REQ/RESP 2250/2251):MODERATOR+
        /// 禁言/解禁 target。durationSec=0 解禁,上限 30 天。返回
        /// SetGroupMuteResponse。连接态检查先于参数校验,非连接态抛
        /// RequestError(Closed)。</summary>
        public async Task<Chirp.Chat.SetGroupMuteResponse> SetGroupMuteAsync(string groupId,
            string targetUserId, long durationSec, int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            var request = new Chirp.Chat.SetGroupMuteRequest
            {
                GroupId = groupId ?? "",
                TargetUserId = targetUserId ?? "",
                DurationSec = durationSec,
            };
            return await RequestAsync(Specs.SetGroupMute, request, timeoutMs).ConfigureAwait(false);
        }

        /// <summary>举报消息(REPORT_MESSAGE_REQ/RESP 2253/2254):服务端内存台账
        /// 按 (message_id, reporter_id) 去重幂等,响应回显累计举报数。reason 可选
        /// (上限 500 码点,服务端校验)。返回 ReportMessageResponse。连接态检查先于
        /// 参数校验,非连接态抛 RequestError(Closed)。</summary>
        public async Task<Chirp.Chat.ReportMessageResponse> ReportMessageAsync(string messageId,
            string reason = "", int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            var request = new Chirp.Chat.ReportMessageRequest
            {
                MessageId = messageId ?? "",
                Reason = reason ?? "",
            };
            return await RequestAsync(Specs.ReportMessage, request, timeoutMs).ConfigureAwait(false);
        }

        /// <summary>防骚扰私聊限制(SET_DM_RESTRICTION_REQ/RESP 2255/2256):
        /// 接收方开关,缺省关。开启后陌生人的私聊被拒 DM_RESTRICTED(消息不落
        /// 任何存储)。开关是服务端 io 线程内存态,进程重启即复位。连接态检查
        /// 先于参数校验,非连接态抛 RequestError(Closed)。</summary>
        public async Task<Chirp.Chat.SetDmRestrictionResponse> SetDmRestrictionAsync(bool enabled,
            int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            var request = new Chirp.Chat.SetDmRestrictionRequest
            {
                Enabled = enabled,
            };
            return await RequestAsync(Specs.SetDmRestriction, request, timeoutMs).ConfigureAwait(false);
        }

        /// <summary>读取防骚扰私聊限制开关态(GET_DM_RESTRICTION_REQ/RESP
        /// 2257/2258)。返回 GetDmRestrictionResponse{code, enabled}。</summary>
        public async Task<Chirp.Chat.GetDmRestrictionResponse> GetDmRestrictionAsync(
            int? timeoutMs = null)
        {
            if (Status != ConnStatus.Connected)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            return await RequestAsync(Specs.GetDmRestriction,
                new Chirp.Chat.GetDmRestrictionRequest(), timeoutMs).ConfigureAwait(false);
        }

        // ----- hook internals -----

        // level: client-declared game level (world-channel level gate,
        // game_chat_features P1). Same trust class as the login token; 0 =
        // undeclared (server gate defaults off).
        private Task<Chirp.Auth.LoginResponse> PostLoginAsync(string token, string deviceId,
            int? timeoutMs, int level)
        {
            return RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest
            {
                Token = token,
                DeviceId = deviceId,
                Platform = "unity",
                SupportsMessageAck = true,
                // 消息压缩消费面：GET_HISTORY 解码侧会拆封 messages_gzip。
                SupportsGzip = true,
                Level = level,
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
                Metadata = options.Metadata,
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
                Metadata = request.Metadata,
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
    }
}
