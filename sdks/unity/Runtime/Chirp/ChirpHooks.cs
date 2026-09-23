using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Chirp.Sdk
{
    /// <summary>
    /// Hook interfaces mirroring the C++ reference SDK's five extension
    /// points (sdks/core/include/chirp/). Semantics are identical; shapes
    /// follow C# idiom (interface + default methods instead of virtual
    /// base classes). All callbacks fire directly on ChirpClient's
    /// receive/call threads — Unity main-thread-only APIs must not be
    /// touched here; marshal through ChirpManager.RunOnMainThread.
    /// Register everything before ConnectAsync() and keep the same
    /// instances alive for the client's lifetime.
    /// </summary>
    public interface IMessageInterceptor
    {
        /// <summary>Send path, after the request is built and validated —
        /// fields may be rewritten in place. Returning false blocks the
        /// message: it never goes on the wire and the caller gets
        /// RequestError(Blocked).</summary>
        bool OnBeforeSend(Chirp.Chat.SendMessageRequest request) => true;

        /// <summary>The request was handed to the wire (any business result,
        /// including a non-OK code, still counts as sent).</summary>
        void OnAfterSend(Chirp.Chat.SendMessageRequest request) { }

        /// <summary>Receive path, before storage and listeners. Returning
        /// false drops the message completely: no local save, no listener,
        /// and it is not dispatched to OnNotify subscribers either.</summary>
        bool OnBeforeReceive(Chirp.Chat.ChatMessage message) => true;

        /// <summary>Receive pipeline finished (save and listeners already
        /// ran). Rewrites from OnBeforeReceive are visible here; the raw
        /// wire body dispatched to OnNotify subscribers is not.</summary>
        void OnAfterReceive(Chirp.Chat.ChatMessage message) { }
    }

    /// <summary>Token source for LoginAsync. With a provider registered, a
    /// login without an explicit token pulls GetToken(); an AUTH_FAILED
    /// response gets exactly one renewal chance (RenewTokenAsync) before
    /// the login fails; every terminal outcome reports through
    /// OnAuthResult. Local errors (RequestError Closed/Timeout) are not
    /// auth results. C++ uses an OnTokenExpired(renew) closure instead of
    /// the awaited RenewTokenAsync — same contract, C# orchestration.</summary>
    public interface IAuthProvider
    {
        /// <summary>Issue a token for this login; empty/null makes the
        /// login fall back to the userId (scaffold-gateway convention).</summary>
        string GetToken();

        /// <summary>One renewal chance after AUTH_FAILED. Return the fresh
        /// token to have the SDK retry the login once with it, or null to
        /// give up (the failure surfaces). Defaults to giving up.</summary>
        Task<string?> RenewTokenAsync() => Task.FromResult<string?>(null);

        /// <summary>Terminal login outcome (success or final failure).</summary>
        void OnAuthResult(Chirp.Common.ErrorCode code, string userId) { }
    }

    /// <summary>Local message archive (对齐 C++ MessageStore). The client
    /// saves every message that passes the interceptor, on both the send
    /// and receive paths, and forwards queries to the registered store.
    /// Concurrency safety is the implementation's job (Save arrives from
    /// the receive thread, queries from wherever the game calls).</summary>
    public interface IMessageStore
    {
        /// <summary>Archive one message (fire-and-forget sends store an
        /// equivalent copy with an empty MessageId — read resp.MessageId
        /// from the send response for the server id).</summary>
        void Save(Chirp.Chat.ChatMessage message);

        /// <summary>Read one channel's archive, newest first.
        /// beforeTimestamp = 0 disables the cutoff.</summary>
        List<Chirp.Chat.ChatMessage> Load(Chirp.Chat.ChannelType type, string channelId,
            int limit, long beforeTimestamp = 0);

        /// <summary>Advance the read cursor. The default implementation
        /// does not track read state.</summary>
        void MarkRead(Chirp.Chat.ChannelType type, string channelId, string messageId) { }

        /// <summary>Unread counter for a channel; the default implementation
        /// always reports 0.</summary>
        int GetUnreadCount(Chirp.Chat.ChannelType type, string channelId) => 0;

        /// <summary>Drop archived messages older than the cutoff.</summary>
        void Cleanup(long olderThanTimestamp) { }
    }

    /// <summary>Game-side listener for client lifecycle events (对齐 C++
    /// ChatEventListener). Every method is optional. Sources wired in this
    /// SDK: connection state, login result, kicked, reconnecting,
    /// reconnected, message received. The remaining callbacks have no
    /// protocol-plane trigger here yet and stay no-ops (same as the C++
    /// reference SDK — presence lives on the social plane 8001).</summary>
    public interface IChatEventListener
    {
        void OnConnectionStateChanged(ConnStatus status) { }

        void OnLoginResult(Chirp.Common.ErrorCode code, string userId) { }

        void OnKicked(string reason) { }

        /// <summary>An automatic reconnect was scheduled (attempt starts at 1).</summary>
        void OnReconnecting(int attempt, int delayMs) { }

        /// <summary>An automatic reconnect succeeded; the session is NOT
        /// restored — replay LOGIN (see ChirpClient.Reconnected).</summary>
        void OnReconnected() { }

        void OnMessageReceived(Chirp.Chat.ChatMessage message) { }

        void OnUnreadChanged(Chirp.Chat.ChannelType type, string channelId, int unread) { }

        void OnPresenceChanged(string userId, bool online) { }

        void OnTypingIndicator(Chirp.Chat.TypingIndicator indicator) { }

        void OnMarqueeMessage(string content) { }

        void OnSystemAnnouncement(string content) { }
    }

    /// <summary>Local '/'-command handler (对齐 C++ CommandHandler). Once at
    /// least one handler is registered, "/name args" texts are routed
    /// locally and never sent; a miss is dropped locally too. With zero
    /// handlers registered, '/' texts pass through as ordinary messages.</summary>
    public interface ICommandHandler
    {
        string Name { get; }

        string Description { get; }

        /// <summary>Usage line; defaults to "/" + Name.</summary>
        string Usage => "/" + Name;

        /// <summary><paramref name="args"/> is the text after the command
        /// name (spaces preserved). Return false to let the next handler
        /// with the same name try; all false = dropped locally.</summary>
        bool Execute(string args, string senderId);
    }

    /// <summary>Who/where/what of a chat send (对齐 C++ ChatClient::SendOptions).
    /// Private sends normalize channel_id to the sorted "a|b" pair
    /// internally; other channel types need an explicit ChannelId.</summary>
    public sealed class SendOptions
    {
        public Chirp.Chat.ChannelType ChannelType { get; set; } =
            Chirp.Chat.ChannelType.Private;

        public string ChannelId { get; set; } = "";

        public string ReceiverId { get; set; } = "";

        /// <summary>被引用回复的消息 id;空 = 非引用。</summary>
        public string ReplyToMessageId { get; set; } = "";

        public Chirp.Chat.MsgType MsgType { get; set; } = Chirp.Chat.MsgType.Text;
    }
}
