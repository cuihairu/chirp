package chirp.mobile

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import chirp.mobile.protocol.ChatConnection
import chirp.mobile.protocol.ChatPipeline
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.DeviceRegistrar
import chirp.mobile.protocol.MemoryMessageStore
import chirp.mobile.protocol.MessageInterceptor
import chirp.mobile.protocol.OkHttpTransport
import chirp.mobile.protocol.OfflineSendQueue
import chirp.mobile.protocol.RequestError
import chirp.mobile.protocol.SendOptions
import chirp.mobile.protocol.WordFilter
import chirp.mobile.protocol.WordFilterLoader
import chirp.mobile.protocol.WordFilterOptions
import chirp.mobile.push.PlatformPushTokenSource
import com.google.protobuf.ByteString
import okhttp3.OkHttpClient
import java.io.File
import java.util.UUID
import java.util.concurrent.TimeUnit

/**
 * M4 dev shell: the M3 login/DM surface rewired onto the [ChatPipeline] —
 * command routing, interceptor (word filter) rewrite, the local
 * [MemoryMessageStore] archive and an [OfflineSendQueue] that replays sends
 * attempted while the link was down on the next reconnect.
 *
 * 假设（非交互自行决定，与 M3 相同）：主机固定 ws://10.0.2.2:7001 —— Android
 * 模拟器里 10.0.2.2 是宿主机 loopback 的别名，与 dart 端 dev 默认一致；真机
 * 联调时再引入可配置主机。词库来源是 filesDir/word_filter.txt（服务端格
 * 式：每行一词、# 注释；无该文件则空词库放行）——协议面没有词库下发消息，
 * 见 docs/design-notes/word_filter.md 的「词库装载」说明。
 */
class MainActivity : Activity() {

    private val main = Handler(Looper.getMainLooper())
    private val okClient = OkHttpClient.Builder()
        .pingInterval(0, TimeUnit.MILLISECONDS) // 心跳在协议层，别让 okhttp 抢发 ping 帧
        .readTimeout(0, TimeUnit.MILLISECONDS) // WebSocket 长连不受读超时约束
        .build()

    private var conn: ChatConnection? = null
    private var pipeline: ChatPipeline? = null
    private var offlineQueue: OfflineSendQueue? = null

    /** filesDir/word_filter.txt 决定；无文件 = 空词库（服务端仍强制自己的过滤）。 */
    private val wordFilter: WordFilter by lazy {
        val lexicon = File(filesDir, LEXICON_FILE)
        if (lexicon.isFile) {
            WordFilterLoader.load(lexicon.inputStream().reader())
        } else {
            WordFilter(WordFilterOptions())
        }
    }

    private lateinit var userInput: EditText
    private lateinit var peerInput: EditText
    private lateinit var loginBtn: Button
    private lateinit var sendBtn: Button
    private lateinit var statusView: TextView
    private lateinit var chatView: TextView
    private lateinit var messageInput: EditText

    private val chatLog = StringBuilder()
    private var selfId: String? = null

    /** dart 端 auth.dart 同款：随机 UUID 首次生成后持久化，随登录上报。 */
    private fun deviceId(): String {
        val prefs = getPreferences(MODE_PRIVATE)
        prefs.getString(KEY_DEVICE_ID, null)?.let { return it }
        val fresh = UUID.randomUUID().toString()
        prefs.edit().putString(KEY_DEVICE_ID, fresh).apply()
        return fresh
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        userInput = findViewById(R.id.user_input)
        peerInput = findViewById(R.id.peer_input)
        loginBtn = findViewById(R.id.login_btn)
        sendBtn = findViewById(R.id.send_btn)
        statusView = findViewById(R.id.status_view)
        chatView = findViewById(R.id.chat_view)
        messageInput = findViewById(R.id.message_input)

        loginBtn.setOnClickListener { login() }
        sendBtn.setOnClickListener { send() }
    }

    override fun onDestroy() {
        super.onDestroy()
        pipeline?.stop()
        conn?.disconnect()
        conn = null
        pipeline = null
        offlineQueue = null
        okClient.dispatcher.executorService.shutdown()
        okClient.connectionPool.evictAll()
    }

    private fun login() {
        val userId = userInput.text.toString().trim()
        if (userId.isEmpty()) {
            toast("先填 user id")
            return
        }
        loginBtn.isEnabled = false
        setStatus("connecting…")

        // 每次登录都换新连接（旧连接若在，直接丢弃断开）。
        pipeline?.stop()
        conn?.disconnect()

        // transportFactory 不是末位参数，trailing lambda 会绑到 scheduler 上，须具名。
        val fresh = ChatConnection(
            url = DEV_HOST,
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
        freshPipeline.interceptor = sendSideWordFilter
        freshPipeline.addListener(shellListener())
        // 断线重连成功后重放离线发送。
        val queue = OfflineSendQueue(
            send = { options, content -> freshPipeline.send(options, content) },
        )
        offlineQueue = queue
        wireMessageAcks(fresh)

        fresh.connect()
            .thenCompose {
                freshPipeline.start()
                freshPipeline.login(userId)
            }
            .whenComplete({ code, err ->
                main.post {
                    when {
                        err != null -> {
                            setStatus("login failed: ${err.cause ?: err}")
                            loginBtn.isEnabled = true
                        }
                        code != Common.ErrorCode.OK -> {
                            setStatus("login rejected: $code")
                            loginBtn.isEnabled = true
                        }
                        else -> {
                            selfId = userId
                            setStatus("logged in as $userId")
                            sendBtn.isEnabled = true
                            loginBtn.isEnabled = true
                            registerForPush(fresh, userId)
                        }
                    }
                }
            })
    }

    /** 发送侧词库预检：REPLACE 语义改写进管线，拦截交给词库策略。 */
    private val sendSideWordFilter = object : MessageInterceptor {
        override fun onBeforeSend(request: Chat.SendMessageRequest): Chat.SendMessageRequest {
            val result = wordFilter.filter(request.content.toStringUtf8())
            if (!result.allowed) {
                throw RequestBlocked("message blocked by word filter")
            }
            if (result.content == request.content.toStringUtf8()) return request
            return request.toBuilder()
                .setContent(ByteString.copyFromUtf8(result.content))
                .build()
        }
    }

    /**
     * M3.5：登录成功后注册推送目标（设备面 app_gateway 5201）。token 由构建
     * 开关决定真假（-PchirpPush=true 走 Firebase，默认构建恒 null），注册本
     * 身始终执行——dart device_api 降级对齐：空 token 也登记，推送退化为服
     * 务端日志投递。
     */
    private fun registerForPush(conn: ChatConnection, userId: String) {
        val registrar = DeviceRegistrar(
            conn = conn,
            deviceId = ::deviceId,
            appVersion = runCatching {
                packageManager.getPackageInfo(packageName, 0).versionName
            }.getOrNull() ?: "",
            osVersion = { android.os.Build.VERSION.RELEASE },
            deviceName = { "${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}" },
        )
        registrar.register(userId, PlatformPushTokenSource(this))
            .whenComplete { code, err ->
                main.post {
                    when {
                        err != null -> appendLine("[device reg] failed: $err")
                        code != Common.ErrorCode.OK -> appendLine("[device reg] rejected: $code")
                        else -> appendLine("[device reg] registered for push")
                    }
                }
            }
    }

    private class RequestBlocked(message: String) : RuntimeException(message)

    private fun shellListener() = object : chirp.mobile.protocol.ChatEventListener {
        override fun onConnectionStateChanged(state: ConnState) {
            main.post {
                when (state) {
                    ConnState.WAITING_RECONNECT -> setStatus("connection lost — reconnecting…")
                    ConnState.CONNECTED -> if (selfId != null) setStatus("connected as $selfId")
                    ConnState.KICKED -> {
                        setStatus("kicked by another login")
                        sendBtn.isEnabled = false
                    }
                    else -> {}
                }
            }
        }

        override fun onKicked(reason: String) {
            main.post { appendLine("[kicked] $reason") }
        }

        override fun onMessageReceived(message: Chat.ChatMessage) {
            main.post { appendLine("[${message.senderId} → me] ${message.content.toStringUtf8()}") }
        }
    }

    /** CHAT_MESSAGE_NOTIFY 渲染 ack（supportsMessageAck=true 的强制回执）。 */
    private fun wireMessageAcks(conn: ChatConnection) {
        conn.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY) { body ->
            val msg = try {
                Chat.ChatMessage.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify
            }
            // Ack 先于渲染：服务器 10s 收不到 ack 会把投递回滚进离线队列。
            runCatching {
                conn.send(
                    Gateway.MsgID.MESSAGE_ACK,
                    Chat.MessageAck.newBuilder()
                        .setMessageId(msg.messageId)
                        .setUserId(selfId ?: "")
                        .setReceivedAt(System.currentTimeMillis())
                        .build()
                        .toByteArray(),
                )
            }
        }
    }

    private fun send() {
        val userId = selfId ?: return
        val peer = peerInput.text.toString().trim()
        if (peer.isEmpty()) {
            toast("先填对方 user id")
            return
        }
        val content = messageInput.text.toString()
        if (content.isEmpty()) return
        messageInput.setText("")
        val options = SendOptions(
            channelType = Chat.ChannelType.PRIVATE,
            receiverId = peer,
        )
        val sent = pipeline?.send(options, content)
        if (sent == null) {
            toast("尚未登录")
            return
        }
        sent.whenComplete({ resp, err ->
            // CompletableFuture 链上的异常不走 ExecutionException 包装，err 就是原始异常。
            val clientId = "${userId}:${content.hashCode()}"
            when {
                err is RequestError && err.kind == RequestError.Kind.CLOSED -> {
                    // 断线：进离线队列，重连后由 onReconnected 重放。
                    offlineQueue?.enqueue(clientId, options, content)
                    main.post { appendLine("[queued offline] $content") }
                }
                err != null -> main.post { appendLine("[!] send failed: $err") }
                else -> main.post { appendLine("[me → $peer] ${resp.code}") }
            }
        })
        wireOfflineFlushOnReconnect()
    }

    private var flushWired = false

    private fun wireOfflineFlushOnReconnect() {
        val conn = this.conn ?: return
        if (flushWired) return
        flushWired = true
        conn.onReconnected {
            val queue = offlineQueue
            if (queue != null && queue.size() > 0) {
                queue.flush().whenComplete { confirmed, _ ->
                    main.post {
                        if (confirmed > 0) appendLine("[offline replay] $confirmed sent")
                    }
                }
            }
        }
    }

    private fun setStatus(text: String) {
        statusView.text = text
    }

    private fun appendLine(line: String) {
        chatLog.append(line).append('\n')
        chatView.text = chatLog
    }

    private fun toast(text: String) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
    }

    private companion object {
        /** 模拟器宿主 loopback 别名：7001 = chat gateway dev 端口。 */
        const val DEV_HOST = "ws://10.0.2.2:7001"
        const val KEY_DEVICE_ID = "device_id"
        const val LEXICON_FILE = "word_filter.txt"
    }
}
