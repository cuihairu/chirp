package chirp.mobile

import android.content.Context
import android.os.Handler
import android.os.Looper
import chirp.chat.Chat
import chirp.common.Common
import chirp.mobile.protocol.ChatConnection
import chirp.mobile.protocol.ChatEventListener
import chirp.mobile.protocol.ChatPipeline
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.DeviceRegistrar
import chirp.mobile.protocol.MessageSpec
import chirp.mobile.protocol.MemoryMessageStore
import chirp.mobile.protocol.OkHttpTransport
import chirp.mobile.protocol.OfflineSendQueue
import chirp.mobile.protocol.PushTokenHub
import chirp.mobile.protocol.PushTokenSource
import chirp.mobile.protocol.RequestError
import chirp.mobile.protocol.SendOptions
import chirp.mobile.protocol.WordFilterSync
import chirp.mobile.push.PlatformPushTokenSource
import com.google.protobuf.MessageLite
import okhttp3.OkHttpClient
import java.io.File
import java.util.UUID
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

/** One row of the sessions list (design 01): a DM pair or a group. */
data class Conversation(
    /** "p:a|b" (sorted pair) or "g:<groupId>" — stable navigation key. */
    val key: String,
    val isGroup: Boolean,
    /** Wire channelId: the sorted "a|b" pair for DMs, group id for groups. */
    val channelId: String,
    val title: String,
    val lastPreview: String,
    val lastTimestamp: Long,
    val unread: Int,
)

/**
 * Process-scoped chat session. The M4 debug console built its
 * connection/pipeline stack per activity and it died with the activity; the
 * redesigned multi-screen shell (CHIRP-UX-001 step 3) needs the stack to
 * outlive any single screen, so it lives here under [ChirpApp] and screens
 * observe it.
 *
 * Behavior already shipped in the console is ported verbatim: scaffold login
 * (dev token = user id), mandatory MESSAGE_ACK before rendering, offline
 * replay on reconnect, send-side word filter (delivered lexicon with the
 * filesDir fallback), device registration with FCM-token refresh fan-out.
 * New here: the conversation registry feeding the sessions list — built from
 * live traffic only (no server-side conversation index exists on this
 * protocol; same fidelity bar as the desktop/web v1).
 */
class ChirpSession(private val context: Context) {

    /** UI-facing fan-out; every callback arrives on the main thread. */
    interface Listener {
        fun onConnectionStateChanged(state: ConnState) {}
        fun onKicked(reason: String) {}
        fun onMessageReceived(message: Chat.ChatMessage) {}
        fun onConversationsChanged() {}
    }

    private val main = Handler(Looper.getMainLooper())
    private val listeners = ArrayList<Listener>()

    val okClient: OkHttpClient = OkHttpClient.Builder()
        .pingInterval(0, TimeUnit.MILLISECONDS) // heartbeat is protocol-level
        .readTimeout(0, TimeUnit.MILLISECONDS) // WebSocket lives past read timeouts
        .build()

    var conn: ChatConnection? = null; private set
    var pipeline: ChatPipeline? = null; private set
    var selfId: String? = null; private set
    var offlineQueue: OfflineSendQueue? = null; private set
    private var wordFilterSync: WordFilterSync? = null
    private var pushRegistrar: DeviceRegistrar? = null
    private var flushWired = false

    private val registry = HashMap<String, Conversation>()

    /** dart auth.dart parity: random UUID persisted on first use. */
    private fun deviceId(): String {
        val prefs = context.getSharedPreferences(SESSION_PREFS, Context.MODE_PRIVATE)
        prefs.getString(KEY_DEVICE_ID, null)?.let { return it }
        val fresh = UUID.randomUUID().toString()
        prefs.edit().putString(KEY_DEVICE_ID, fresh).apply()
        return fresh
    }

    fun login(host: String, userId: String): CompletableFuture<Common.ErrorCode> {
        pipeline?.stop()
        conn?.disconnect()

        // transportFactory is not the last parameter, so the trailing-lambda
        // form would bind to the scheduler — pass it named.
        val fresh = ChatConnection(
            url = host,
            transportFactory = { url -> OkHttpTransport(url, okClient) },
        )
        conn = fresh
        val freshPipeline = ChatPipeline(
            conn = fresh,
            selfId = { this.selfId ?: "" },
            deviceId = ::deviceId,
        )
        pipeline = freshPipeline
        freshPipeline.store = MemoryMessageStore()
        val sync = freshWordFilterSync(fresh)
        wordFilterSync = sync
        freshPipeline.interceptor = sync
        freshPipeline.addListener(pipelineListener())
        val queue = OfflineSendQueue(
            send = { options, content -> freshPipeline.send(options, content) },
        )
        offlineQueue = queue
        wireOfflineFlushOnReconnect(fresh)
        wireMessageAcks(fresh)

        return fresh.connect()
            .thenCompose {
                freshPipeline.start()
                freshPipeline.login(userId)
            }
            .thenApply { code ->
                if (code == Common.ErrorCode.OK) {
                    selfId = userId
                    main.post {
                        registerForPush(fresh, userId)
                        // Delivered lexicon: one fetch right after login;
                        // failures fall back to the local file silently.
                        wordFilterSync?.fetch()
                    }
                }
                code
            }
    }

    fun logout() {
        pipeline?.stop()
        conn?.disconnect()
        conn = null
        pipeline = null
        offlineQueue = null
        wordFilterSync?.stop()
        wordFilterSync = null
        pushRegistrar = null
        selfId = null
        flushWired = false
        synchronized(registry) { registry.clear() }
        PushTokenHub.removeListener(pushTokenRefresh)
        notifyConversationsChanged()
    }

    /** Typed request seam for the feature planes (friends/groups/party/…). */
    fun <T : MessageLite> request(
        spec: MessageSpec<T>,
        body: MessageLite,
    ): CompletableFuture<T> {
        val c = conn
            ?: return CompletableFuture.failedFuture(RequestError(RequestError.Kind.CLOSED))
        return c.request(spec, body)
    }

    /** Full-pipeline send (command routing + word filter + archive). */
    fun send(options: SendOptions, content: String): CompletableFuture<Chat.SendMessageResponse>? =
        pipeline?.send(options, content)

    // ---- sessions list ---------------------------------------------------

    fun conversations(): List<Conversation> = synchronized(registry) {
        registry.values.sortedByDescending { it.lastTimestamp }
    }

    fun conversationByKey(key: String): Conversation? = synchronized(registry) { registry[key] }

    fun markRead(channelType: Chat.ChannelType, channelId: String, messageId: String) {
        pipeline?.markRead(channelType, channelId, messageId)
        refreshUnread(channelType, channelId)
    }

    /** Registry upsert after a locally sent message is confirmed. */
    fun noteLocalSend(channelType: Chat.ChannelType, channelId: String, content: String) {
        main.post {
            upsert(channelType, channelId, selfId ?: "", content, System.currentTimeMillis())
        }
    }

    // ---- listeners -------------------------------------------------------

    fun addListener(listener: Listener): () -> Unit = synchronized(listeners) {
        listeners.add(listener)
        return { synchronized(listeners) { listeners.remove(listener) } }
    }

    private fun emit(block: (Listener) -> Unit) {
        val snapshot = synchronized(listeners) { listeners.toList() }
        for (l in snapshot) {
            runCatching { block(l) } // a throwing screen never kills the session
        }
    }

    private fun pipelineListener() = object : ChatEventListener {
        override fun onConnectionStateChanged(state: ConnState) {
            main.post { emit { it.onConnectionStateChanged(state) } }
        }

        override fun onKicked(reason: String) {
            main.post { emit { it.onKicked(reason) } }
        }

        override fun onMessageReceived(message: Chat.ChatMessage) {
            main.post {
                upsert(message.channelType, message.channelId, message.senderId,
                    message.content.toStringUtf8(), message.timestamp)
                emit { it.onMessageReceived(message) }
            }
        }
    }

    private fun notifyConversationsChanged() = emit { it.onConversationsChanged() }

    // ---- registry internals ----------------------------------------------

    private fun conversationKey(channelType: Chat.ChannelType, channelId: String): String? =
        when (channelType) {
            Chat.ChannelType.PRIVATE -> "p:$channelId" // wire id is the sorted pair
            Chat.ChannelType.GUILD -> "g:$channelId"
            else -> null // world/system channels have no conversation face (v1)
        }

    private fun upsert(
        channelType: Chat.ChannelType,
        channelId: String,
        senderId: String,
        content: String,
        timestamp: Long,
    ) {
        val key = conversationKey(channelType, channelId) ?: return
        val self = selfId ?: return
        val isGroup = channelType == Chat.ChannelType.GUILD
        val title = if (isGroup) {
            channelId
        } else {
            channelId.split('|').firstOrNull { it != self } ?: channelId
        }
        val preview = if (isGroup && senderId != self) "$senderId: $content" else content
        val unread = pipeline?.unreadCount(channelType, channelId) ?: 0
        synchronized(registry) {
            registry[key] = Conversation(key, isGroup, channelId, title, preview, timestamp, unread)
        }
        notifyConversationsChanged()
    }

    private fun refreshUnread(channelType: Chat.ChannelType, channelId: String) {
        val key = conversationKey(channelType, channelId) ?: return
        synchronized(registry) {
            val prev = registry[key] ?: return
            val unread = pipeline?.unreadCount(channelType, channelId) ?: 0
            registry[key] = prev.copy(unread = unread)
        }
        notifyConversationsChanged()
    }

    // ---- session helpers (ported from the M4 console) ---------------------

    /** Delivered lexicon wins; filesDir/word_filter.txt is the fallback. */
    private fun freshWordFilterSync(conn: ChatConnection): WordFilterSync =
        WordFilterSync(conn).also { sync ->
            val lexicon = File(context.filesDir, LEXICON_FILE)
            if (lexicon.isFile) {
                sync.loadLocal(lexicon.readLines())
            }
            sync.start()
        }

    /** MESSAGE_ACK before render: the server rolls delivery back after 10s. */
    private fun wireMessageAcks(conn: ChatConnection) {
        conn.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY) { body ->
            val msg = try {
                Chat.ChatMessage.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify
            }
            runCatching {
                conn.send(
                    Gateway.MsgID.MESSAGE_ACK,
                    Chat.MessageAck.newBuilder()
                        .setMessageId(msg.messageId)
                        .setDeliveryId(msg.deliveryId)
                        .setUserId(selfId ?: "")
                        .setReceivedAt(System.currentTimeMillis())
                        .build()
                        .toByteArray(),
                )
            }
        }
    }

    private fun wireOfflineFlushOnReconnect(conn: ChatConnection) {
        if (flushWired) return
        flushWired = true
        conn.onReconnected {
            val queue = offlineQueue
            if (queue != null && queue.size() > 0) {
                queue.flush().whenComplete { confirmed, _ ->
                    main.post { notifyConversationsChanged() }
                }
            }
        }
    }

    /** FCM token refresh mid-session → re-register on the live connection. */
    private val pushTokenRefresh = PushTokenHub.Listener { token ->
        main.post { reRegisterForPush(token) }
    }

    private fun reRegisterForPush(token: String) {
        val registrar = pushRegistrar ?: return
        val user = selfId ?: return
        registrar.register(user, PushTokenSource { onResult -> onResult(token) })
            .whenComplete { _, _ -> } // refresh outcome is invisible by design
    }

    private fun registerForPush(conn: ChatConnection, userId: String) {
        val registrar = DeviceRegistrar(
            conn = conn,
            deviceId = ::deviceId,
            appVersion = runCatching {
                context.packageManager.getPackageInfo(context.packageName, 0).versionName
            }.getOrNull() ?: "",
            osVersion = { android.os.Build.VERSION.RELEASE },
            deviceName = { "${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}" },
        )
        pushRegistrar = registrar
        // Re-login swaps the registrar: detach the old listener first so the
        // fan-out never doubles.
        PushTokenHub.removeListener(pushTokenRefresh)
        PushTokenHub.addListener(pushTokenRefresh)
        registrar.register(userId, PlatformPushTokenSource(context))
            .whenComplete { _, _ -> } // registration result surfaces on the device screen
    }

    private companion object {
        const val SESSION_PREFS = "chirp_session"
        const val KEY_DEVICE_ID = "device_id"
        const val LEXICON_FILE = "word_filter.txt"
    }
}
