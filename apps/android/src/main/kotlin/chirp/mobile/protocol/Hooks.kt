package chirp.mobile.protocol

import chirp.auth.Auth
import chirp.chat.Chat
import chirp.common.Common

/**
 * Five hook seams for the chat pipeline — the Kotlin face of the same
 * contract the C++ core, the Unity SDK, the web protocol layer and the dart
 * ChatPipeline ship (hooks.dart is the direct sibling).
 *
 * Kotlin delta vs dart: protobuf messages are immutable here, so the
 * interceptors' "mutate in place, return bool" contract becomes "return the
 * (possibly rewritten) message, or null to drop". Everything else keeps the
 * dart/C++/C# semantics, including "zero hooks = zero change" and "a
 * throwing hook is a blocking/declining hook, never a crash".
 */

/** Send parameters for [ChatPipeline.send] (C++/C#/dart SendOptions). */
class SendOptions(
    val channelType: Chat.ChannelType,
    /** Required for every non-private channel; private derives the sorted pair. */
    val channelId: String? = null,
    /** Required for private; the wire channel id is the sorted "a|b" pair. */
    val receiverId: String? = null,
    /** Quoted message id; empty = not a reply. */
    val replyToMessageId: String = "",
    val msgType: Chat.MsgType = Chat.MsgType.TEXT,
)

/**
 * Rewrite/audit points around send & receive. Returning null from
 * onBeforeSend/onBeforeReceive drops the message; the receive-side drop also
 * skips the archive and the listeners.
 *
 * 重入约束（与 C++「钩子在 io 线程触发」同款）：接收侧钩子在连接锁内被调
 * 用——钩子里不得回调连接（request/send/onNotify 都会死锁）；发送侧钩子在
 * 调用方线程，无此限制。
 */
interface MessageInterceptor {
    /** null = the message never reaches the wire; else the (rewritten) request. */
    fun onBeforeSend(request: Chat.SendMessageRequest): Chat.SendMessageRequest? = request
    fun onAfterSend(request: Chat.SendMessageRequest) {}

    /** null = dropped for the archive, the listeners and onAfterReceive. */
    fun onBeforeReceive(message: Chat.ChatMessage): Chat.ChatMessage? = message
    fun onAfterReceive(message: Chat.ChatMessage) {}
}

/**
 * Token sourcing for login + one renewal on AUTH_FAILED. at most one renewal
 * per login chain (C++ OnTokenExpired / C# RenewTokenAsync parity).
 */
interface AuthProvider {
    /** Used when no explicit token is passed to [ChatPipeline.login]. */
    fun getToken(): String

    /**
     * AUTH_FAILED gives this one chance to hand back a fresh token (the
     * pipeline retries the login round once). Returning null ends the login
     * with the failure.
     */
    fun renewToken(): String? = null

    /** Terminal auth outcome: success or final failure. */
    fun onAuthResult(code: Common.ErrorCode, userId: String) {}
}

/**
 * Local archive; the pipeline saves both directions (sent copies carry an
 * empty messageId). Implementations must be safe to call from the connection
 * callback thread; they must not re-enter the connection.
 */
interface MessageStore {
    fun save(message: Chat.ChatMessage)

    /** Newest first; beforeTimestamp 0/null = no bound. */
    fun load(
        channelType: Chat.ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Long = 0,
    ): List<Chat.ChatMessage>

    /** Without an implementation the pipeline reports 0 (C++/C# parity). */
    fun getUnreadCount(channelType: Chat.ChannelType, channelId: String): Int = 0

    fun markRead(channelType: Chat.ChannelType, channelId: String, messageId: String) {}

    fun cleanup(olderThanMs: Long) {}
}

/**
 * In-memory archive keyed by `$channelType|$channelId`. Newest-first loads,
 * oldest-first eviction beyond the cap — the C#/C++/TS MemoryMessageStore
 * semantics. markRead/getUnreadCount are not tracked (0, like the reference).
 *
 * Kotlin: protobuf messages are immutable, so the archive needs no snapshot
 * copy (dart deepCopy's for exactly the aliasing this port cannot have).
 */
class MemoryMessageStore(private val maxPerChannel: Int = 200) : MessageStore {
    private val channels = HashMap<String, ArrayDeque<Chat.ChatMessage>>()

    override fun save(message: Chat.ChatMessage) {
        val key = "${message.channelTypeValue}|${message.channelId}"
        val bucket = channels.getOrPut(key) { ArrayDeque() }
        bucket.addLast(message)
        if (maxPerChannel > 0 && bucket.size > maxPerChannel) {
            bucket.removeFirst()
        }
    }

    override fun load(
        channelType: Chat.ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Long,
    ): List<Chat.ChatMessage> {
        if (limit <= 0) return emptyList()
        // ChannelType is a proto3 enum: the wire value is what the archive
        // keys on (dart keys on .value identically).
        val bucket = channels["${channelType.number}|$channelId"] ?: return emptyList()
        val out = ArrayList<Chat.ChatMessage>(limit)
        for (message in bucket.asReversed()) {
            if (out.size >= limit) break
            if (beforeTimestamp == 0L || message.timestamp < beforeTimestamp) {
                out.add(message)
            }
        }
        return out
    }

    override fun cleanup(olderThanMs: Long) {
        val deadKeys = ArrayList<String>()
        channels.forEach { (key, bucket) ->
            bucket.removeAll { it.timestamp < olderThanMs }
            if (bucket.isEmpty()) deadKeys.add(key)
        }
        deadKeys.forEach { channels.remove(it) }
    }
}

/**
 * Lifecycle listener. onReconnecting/onReconnected/onMessageReceived are
 * wired by [ChatPipeline]; onLoginResult/onKicked/onConnectionStateChanged
 * are the terminal reports. Unread/presence/typing/marquee/announcement have
 * no trigger source on this plane yet — leave them untouched.
 */
interface ChatEventListener {
    fun onConnectionStateChanged(state: ConnState) {}
    fun onLoginResult(code: Common.ErrorCode, userId: String) {}
    fun onKicked(reason: String) {}
    fun onReconnecting(attempt: Int, delayMs: Long) {}
    fun onReconnected() {}
    fun onMessageReceived(message: Chat.ChatMessage) {}

    /** 多端在线（P0）：某端上线/下线/被顶时的清单变更（DEVICES_PRESENCE_NOTIFY）。 */
    fun onDevicesPresence(devices: List<Auth.DevicePresence>) {}

    /** 多端在线（P0）：登录成功时该用户其他在线端的初始清单。 */
    fun onLoginDevices(devices: List<Auth.DevicePresence>) {}
}

/**
 * Local '/'-command routing. Commands are matched in registration order; a
 * throwing execute declines (the next same-name handler gets a try).
 */
interface CommandHandler {
    /** Command word after the '/'. */
    val name: String

    /** Display form; defaults to `/$name` like the C++/C# GetUsage default. */
    val usage: String get() = "/$name"

    /** false = the next handler with the same name gets a try. */
    fun execute(args: String, senderId: String): Boolean
}
