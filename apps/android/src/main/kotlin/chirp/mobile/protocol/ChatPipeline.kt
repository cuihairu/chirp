package chirp.mobile.protocol

import chirp.auth.Auth
import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import java.util.concurrent.CompletableFuture

/**
 * The hook host for the Kotlin plane, structurally the sibling of ChatClient
 * on C++ (sdks/core), Unity/.NET, the web protocol layer and the dart
 * ChatPipeline (chat_pipeline.dart, faithful port): command routing,
 * interceptor rewrite/drop, local archive and lifecycle listeners around a
 * plain request/notify connection.
 *
 * Kotlin deltas vs dart (both documented, both deliberate):
 * - send()/login() return [CompletableFuture]s; every pre-wire failure
 *   (closed, blocked, bad arguments) completes them exceptionally instead of
 *   dart's synchronous throws — one error channel for callers.
 * - Interceptors swap "mutate in place" for "return the rewritten message or
 *   null" (protobuf immutability), see [MessageInterceptor].
 *
 * Ordering guarantees are preserved exactly: an onBeforeReceive drop kills
 * the archive, the listeners and onAfterReceive; other notify subscribers
 * still see the original wire body, untouched by interceptor rewrites. A
 * throwing hook never starves its siblings and never crashes the pipeline.
 *
 * Threading: hook registrations are internally synchronized; the incoming
 * path runs on the connection callback thread under the connection lock —
 * hooks must not re-enter the connection from there (deadlock), mirroring
 * the C++ "hooks fire on the io thread" contract. The connection must
 * already be connected for login/send (dart parity: the caller connects).
 */
class ChatPipeline(
    private val conn: ChatConnection,
    private val selfId: () -> String,
    private val deviceId: () -> String,
) {
    @Volatile
    var interceptor: MessageInterceptor? = null

    @Volatile
    var provider: AuthProvider? = null

    @Volatile
    var store: MessageStore? = null

    private val listeners = ArrayList<ChatEventListener>()
    private val commands = ArrayList<CommandHandler>()
    private val unsubs = ArrayList<() -> Unit>()
    private val lock = Any()

    // ---- hook registration (C++/C# setter parity) --------------------------

    /** Returns the unsubscribe function. */
    fun addListener(listener: ChatEventListener): () -> Unit = synchronized(lock) {
        listeners.add(listener)
        return@synchronized { synchronized(lock) { listeners.remove(listener) } }
    }

    /** Returns the unsubscribe function. */
    fun registerCommand(handler: CommandHandler): () -> Unit = synchronized(lock) {
        commands.add(handler)
        return@synchronized { synchronized(lock) { commands.remove(handler) } }
    }

    // ---- lifecycle wiring ---------------------------------------------------

    /** Idempotent; call again after [stop]. */
    fun start() {
        synchronized(lock) {
            if (unsubs.isNotEmpty()) return
            unsubs += conn.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, ::onIncoming)
            unsubs += conn.onNotify(Gateway.MsgID.KICK_NOTIFY, ::onKickBody)
            unsubs += conn.onNotify(Gateway.MsgID.DEVICES_PRESENCE_NOTIFY, ::onDevicesPresenceBody)
            unsubs += conn.onStateChange { state ->
                listeners.forEach { l ->
                    runCatching { l.onConnectionStateChanged(state) }
                }
            }
            unsubs += conn.onReconnecting { attempt, delayMs ->
                listeners.forEach { l ->
                    runCatching { l.onReconnecting(attempt, delayMs) }
                }
            }
            unsubs += conn.onReconnected {
                listeners.forEach { l -> runCatching { l.onReconnected() } }
            }
        }
    }

    fun stop() {
        synchronized(lock) {
            unsubs.forEach { runCatching { it() } }
            unsubs.clear()
        }
    }

    /**
     * LOGIN with token sourcing: explicit token wins, then
     * provider.getToken(), then userId itself (scaffold mode). AUTH_FAILED
     * gives the provider one renewal chance (renewToken → a second LOGIN
     * round); at most one renewal per login chain. The terminal outcome fans
     * out onLoginResult + onAuthResult. Completes with the final code
     * (OK = logged in).
     */
    fun login(userId: String, token: String? = null): CompletableFuture<Common.ErrorCode> {
        val first = loginRound(token ?: provider?.getToken() ?: userId)
        return first.thenCompose { code ->
            val fresh = if (code == Common.ErrorCode.AUTH_FAILED) provider?.renewToken() else null
            if (fresh != null) loginRound(fresh) else CompletableFuture.completedFuture(code)
        }.thenApply { code ->
            terminalAuth(code, userId)
            code
        }
    }

    /**
     * Full-pipeline send. Fails the future with [RequestError.Kind.CLOSED]
     * when not connected, [RequestError.Kind.BLOCKED] for messages consumed
     * before the wire (local command, unknown command, interceptor drop),
     * and [IllegalArgumentException] for invalid arguments (C#
     * ArgumentException parity). A returned response may still carry a
     * non-OK code (rate limit, invalid param…); read resp.code.
     */
    fun send(options: SendOptions, content: String): CompletableFuture<Chat.SendMessageResponse> {
        if (conn.state != ConnState.CONNECTED) {
            // 状态检查先于参数校验(C++ 参考实现顺序)。
            return failed(RequestError(RequestError.Kind.CLOSED))
        }
        if (content.isEmpty()) {
            return failed(IllegalArgumentException("content is empty"))
        }
        if (options.channelType == Chat.ChannelType.PRIVATE) {
            if (options.receiverId.isNullOrEmpty()) {
                return failed(IllegalArgumentException("private send needs receiverId"))
            }
        } else if (options.channelId.isNullOrEmpty()) {
            return failed(
                IllegalArgumentException("${options.channelType} send needs an explicit channelId"),
            )
        }

        if (content.startsWith("/")) {
            val routed = routeCommand(content, selfId())
            if (routed != null) {
                return failed(RequestError(RequestError.Kind.BLOCKED, message = routed))
            }
        }

        val request = buildSendRequest(options, content)

        val interceptor = this.interceptor
        val effective = if (interceptor != null) {
            val rewritten = try {
                interceptor.onBeforeSend(request)
            } catch (_: Exception) {
                null // a throwing interceptor is a blocking one
            }
            if (rewritten == null) {
                return failed(
                    RequestError(RequestError.Kind.BLOCKED, message = "message blocked by interceptor"),
                )
            }
            rewritten
        } else {
            request
        }

        val store = this.store
        if (store != null) {
            runCatching { store.save(storedCopyOf(effective)) }
            // Archive failures must not block the send.
        }

        return conn.request(MsgSpecs.sendMessage, effective).thenApply { resp ->
            runCatching { interceptor?.onAfterSend(effective) }
            // Audit hooks must not break the caller.
            resp
        }
    }

    // ---- archive forwards (C++ LoadHistory/MarkRead/GetUnreadCount/Cleanup) -

    /** Newest-first slice; empty when no store is installed. */
    fun loadHistory(
        channelType: Chat.ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Long = 0,
    ): List<Chat.ChatMessage> =
        store?.load(channelType, channelId, limit, beforeTimestamp) ?: emptyList()

    fun markRead(channelType: Chat.ChannelType, channelId: String, messageId: String) {
        store?.markRead(channelType, channelId, messageId)
    }

    fun unreadCount(channelType: Chat.ChannelType, channelId: String): Int =
        store?.getUnreadCount(channelType, channelId) ?: 0

    fun cleanup(olderThanMs: Long) {
        store?.cleanup(olderThanMs)
    }

    // ---- internals ----------------------------------------------------------

    /** null = pass through (no handlers); a string = the blocked reason. */
    private fun routeCommand(content: String, senderId: String): String? {
        val registered = synchronized(lock) { commands.toList() }
        if (registered.isEmpty()) return null
        var name = content.substring(1)
        var args = ""
        val space = name.indexOf(' ')
        if (space >= 0) {
            args = name.substring(space + 1)
            name = name.substring(0, space)
        }
        var handled = false
        for (command in registered) {
            if (command.name != name) continue
            handled = try {
                command.execute(args, senderId)
            } catch (_: Exception) {
                false // a throwing handler declines, like C++/C#
            }
            if (handled) break
        }
        return if (handled) {
            "command handled locally"
        } else {
            "unknown command, dropped locally: $content"
        }
    }

    private fun buildSendRequest(options: SendOptions, content: String): Chat.SendMessageRequest {
        val senderId = selfId()
        var channelId = options.channelId ?: ""
        if (options.channelType == Chat.ChannelType.PRIVATE) {
            // 双方 id 字典序小者在前,与服务端/c++/c#/web 同款。
            channelId = listOf(senderId, options.receiverId!!).sorted().joinToString("|")
        }
        return Chat.SendMessageRequest.newBuilder()
            .setSenderId(senderId)
            .setReceiverId(options.receiverId ?: "")
            .setChannelType(options.channelType)
            .setChannelId(channelId)
            .setMsgType(options.msgType)
            .setContent(ByteString.copyFromUtf8(content))
            .setClientTimestamp(System.currentTimeMillis())
            .setReplyToMessageId(options.replyToMessageId)
            .build()
    }

    private fun storedCopyOf(request: Chat.SendMessageRequest): Chat.ChatMessage =
        Chat.ChatMessage.newBuilder()
            .setSenderId(request.senderId)
            .setReceiverId(request.receiverId)
            .setChannelType(request.channelType)
            .setChannelId(request.channelId)
            .setMsgType(request.msgType)
            .setContent(request.content)
            .setTimestamp(request.clientTimestamp)
            .setReplyToMessageId(request.replyToMessageId)
            .build()

    /** Interceptor → archive → listeners → onAfterReceive; drop kills all. */
    private fun onIncoming(body: ByteArray) {
        val message = try {
            Chat.ChatMessage.parseFrom(body)
        } catch (_: Exception) {
            return // undecodable: other notify subscribers still got the raw body
        }
        val interceptor = this.interceptor
        val effective = if (interceptor != null) {
            val rewritten = try {
                interceptor.onBeforeReceive(message)
            } catch (_: Exception) {
                null // a throwing interceptor is a blocking one
            }
            if (rewritten == null) return
            rewritten
        } else {
            message
        }
        val store = this.store
        if (store != null) {
            runCatching { store.save(effective) }
            // Archive failures must not kill the fan-out.
        }
        snapshotListeners().forEach { l -> runCatching { l.onMessageReceived(effective) } }
        runCatching { interceptor?.onAfterReceive(effective) }
    }

    private fun onDevicesPresenceBody(body: ByteArray) {
        val devices = try {
            Auth.DevicesPresenceNotify.parseFrom(body).devicesList
        } catch (_: Exception) {
            // 多端在线（P0）：畸形清单丢弃，不影响其他订阅者。
            return
        }
        if (devices.isEmpty()) return
        snapshotListeners().forEach { l -> runCatching { l.onDevicesPresence(devices) } }
    }

    private fun onKickBody(body: ByteArray) {
        val reason = try {
            Auth.KickNotify.parseFrom(body).reason
        } catch (_: Exception) {
            "" // A malformed kick still proves the session is dead.
        }
        snapshotListeners().forEach { l -> runCatching { l.onKicked(reason) } }
    }

    private fun loginRound(token: String): CompletableFuture<Common.ErrorCode> =
        conn.request(
            MsgSpecs.login,
            Auth.LoginRequest.newBuilder()
                .setToken(token)
                .setDeviceId(deviceId())
                .setPlatform("android")
                .build(),
        ).thenApply { resp ->
            if (resp.code == Common.ErrorCode.OK) {
                conn.resetBackoff()
                // 多端在线（P0）：登录响应携带的其他在线端初始清单。
                val devices = resp.onlineDevicesList
                if (devices.isNotEmpty()) {
                    snapshotListeners().forEach { l -> runCatching { l.onLoginDevices(devices) } }
                }
            }
            resp.code
        }

    private fun terminalAuth(code: Common.ErrorCode, userId: String) {
        snapshotListeners().forEach { l -> runCatching { l.onLoginResult(code, userId) } }
        runCatching { provider?.onAuthResult(code, userId) }
    }

    private fun snapshotListeners(): List<ChatEventListener> =
        synchronized(lock) { listeners.toList() }

    private companion object {
        fun <T> failed(error: Throwable): CompletableFuture<T> =
            CompletableFuture.failedFuture(error)
    }
}
