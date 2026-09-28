package chirp.mobile

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import chirp.auth.Auth
import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import chirp.mobile.protocol.ChatConnection
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.MsgSpecs
import chirp.mobile.protocol.OkHttpTransport
import chirp.mobile.protocol.WordFilter
import chirp.mobile.protocol.WordFilterOptions
import okhttp3.OkHttpClient
import java.util.UUID
import java.util.concurrent.TimeUnit

/**
 * M3 dev shell: login with a user id (dev token == user id), DM a peer, see
 * live pushes. Pure framework Views on purpose (no androidx/Compose) and a
 * single Activity on purpose — the M4 batch ports chat_pipeline/offline
 * queue and grows the real navigation around them.
 *
 * 假设（非交互自行决定）：主机固定 ws://10.0.2.2:7001 —— Android 模拟器里
 * 10.0.2.2 是宿主机 loopback 的别名，与 dart 端 dev 默认一致；真机联调时
 * 再引入可配置主机。词库默认空（服务端有自己的 word filter，客户端这里是
 * REPLACE 语义的发送侧预检位，M4 接词库下发）。
 */
class MainActivity : Activity() {

    private val main = Handler(Looper.getMainLooper())
    private val okClient = OkHttpClient.Builder()
        .pingInterval(0, TimeUnit.MILLISECONDS) // 心跳在协议层，别让 okhttp 抢发 ping 帧
        .readTimeout(0, TimeUnit.MILLISECONDS) // WebSocket 长连不受读超时约束
        .build()

    private var conn: ChatConnection? = null
    private val wordFilter = WordFilter(WordFilterOptions())

    private lateinit var userInput: EditText
    private lateinit var peerInput: EditText
    private lateinit var loginBtn: Button
    private lateinit var sendBtn: Button
    private lateinit var statusView: TextView
    private lateinit var chatView: TextView
    private lateinit var messageInput: EditText

    private val chatLog = StringBuilder()
    private var selfId: String? = null
    private val unsubs = mutableListOf<() -> Unit>()

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
        unsubs.forEach { runCatching { it() } }
        unsubs.clear()
        conn?.disconnect()
        conn = null
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
        unsubs.forEach { runCatching { it() } }
        unsubs.clear()
        conn?.disconnect()

        val fresh = ChatConnection(DEV_HOST) { url -> OkHttpTransport(url, okClient) }
        conn = fresh
        wireNotifications(fresh)

        fresh.connect()
            .thenCompose {
                fresh.request(
                    MsgSpecs.login,
                    Auth.LoginRequest.newBuilder()
                        .setToken(userId)
                        .setDeviceId(deviceId())
                        .setPlatform("android")
                        .setSupportsMessageAck(true)
                        .build(),
                )
            }
            .whenComplete({ resp, err ->
                if (err != null || resp.code != Common.ErrorCode.OK) {
                    main.post {
                        setStatus(
                            when {
                                err != null -> "login failed: ${err.cause ?: err}"
                                else -> "login rejected: ${resp.code}"
                            },
                        )
                        loginBtn.isEnabled = true
                    }
                    return@whenComplete
                }
                // dart chat_api.login 同款：登录成功才退避重置。
                fresh.resetBackoff()
                main.post {
                    selfId = userId
                    setStatus("logged in as $userId (${resp.userId}, session ${resp.sessionId})")
                    sendBtn.isEnabled = true
                    loginBtn.isEnabled = true
                }
            })
    }

    /** CHAT_MESSAGE_NOTIFY 渲染 + 强制 MESSAGE_ACK（supportsMessageAck=true）。 */
    private fun wireNotifications(conn: ChatConnection) {
        unsubs += conn.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY) { body ->
            val msg = try {
                Chat.ChatMessage.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify // undecodable：丢弃但不杀连接（协议层已隔离帧错误）
            }
            // Ack 先于渲染：服务器 10s 收不到 ack 会把投递回滚进离线队列。
            val self = selfId ?: ""
            runCatching {
                conn.send(
                    Gateway.MsgID.MESSAGE_ACK,
                    Chat.MessageAck.newBuilder()
                        .setMessageId(msg.messageId)
                        .setUserId(self)
                        .setReceivedAt(System.currentTimeMillis())
                        .build()
                        .toByteArray(),
                )
            }
            main.post { appendLine("[${msg.senderId} → me] ${msg.content.toStringUtf8()}") }
        }
        unsubs += conn.onStateChange { state -> main.post { onState(state) } }
    }

    private fun onState(state: ConnState) {
        when (state) {
            ConnState.WAITING_RECONNECT -> setStatus("connection lost — reconnecting…")
            ConnState.CONNECTED -> if (selfId != null) setStatus("connected as $selfId")
            ConnState.KICKED -> {
                setStatus("kicked by another login")
                sendBtn.isEnabled = false
            }
            ConnState.CLOSED -> if (selfId == null) setStatus("closed")
            else -> {}
        }
    }

    private fun send() {
        val conn = this.conn ?: return
        val userId = selfId ?: return
        val peer = peerInput.text.toString().trim()
        if (peer.isEmpty()) {
            toast("先填对方 user id")
            return
        }
        val raw = messageInput.text.toString()
        val filtered = wordFilter.filter(raw)
        if (!filtered.allowed) {
            toast("消息被敏感词策略拦截")
            return
        }
        messageInput.setText("")
        // 私聊 channelId 与 dart home_screen 一致：两端 id 排序后用 | 拼接。
        val channelId = listOf(userId, peer).sorted().joinToString("|")
        conn.request(
            MsgSpecs.sendMessage,
            Chat.SendMessageRequest.newBuilder()
                .setSenderId(userId)
                .setReceiverId(peer) // 群发必须留空；私聊必填（chat_validation）
                .setChannelType(Chat.ChannelType.PRIVATE)
                .setChannelId(channelId)
                .setMsgType(Chat.MsgType.TEXT)
                .setContent(com.google.protobuf.ByteString.copyFromUtf8(filtered.content))
                .setClientTimestamp(System.currentTimeMillis())
                .build(),
        ).whenComplete({ resp, err ->
            main.post {
                when {
                    err != null -> appendLine("[!] send failed: ${err.cause ?: err}")
                    resp.code == Common.ErrorCode.OK || resp.code == Common.ErrorCode.TARGET_OFFLINE ->
                        appendLine("[me → $peer] ${filtered.content}")
                    else -> appendLine("[!] send rejected: ${resp.code}")
                }
            }
        })
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
    }
}
