package chirp.mobile.protocol

import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import java.util.concurrent.CompletableFuture

/**
 * 词库下发同步：词库下发协议（docs/design-notes/word_filter.md「词库下发协议」，
 * MsgID 2245-2247）的 Android 面。内部持一个可热换的 [WordFilter]：
 * FETCH_RESP / UPDATE_NOTIFY 载荷到达即重建，发送侧预检跟随服务端词库。
 * [loadLocal]（原 WordFilterLoader 面孔）降级为回退：首个服务端词库
 * （version >= 1）到达即接管；服务端应答 version 0（未装配词库）时清空
 * 预检——客户端永远不比服务端更严。
 *
 * 版本门：任何 version <= 本地版本的载荷视为乱序/重复投递，忽略。
 * record 是纯服务端审计语义，客户端不预检（透传）。
 *
 * 协程约定：notify 回调在连接线程，fetch 在调用方线程；[inner] 与
 * [version] 用 @Volatile 读改写，两路载荷最多差一轮（下一轮必然收敛）。
 */
class WordFilterSync(
    private val conn: ChatConnection,
    private val timeoutMs: Long = DEFAULT_TIMEOUT_MS,
) : MessageInterceptor {

    @Volatile
    private var inner: WordFilter? = null

    @Volatile
    private var version: Long = 0

    private var unsubscribe: (() -> Unit)? = null

    /** 当前生效的服务端词库版本；0 = 尚未见过（本地回退不动它）。 */
    val currentVersion: Long get() = version

    /** 预检当前词数（服务端词库或本地回退）。 */
    val wordCount: Int get() = inner?.wordCount ?: 0

    /** 订阅 UPDATE_NOTIFY。登录成功后调一次。 */
    fun start() {
        unsubscribe = conn.onNotify(Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY) { body ->
            val notify = try {
                Chat.WordFilterUpdateNotify.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify // 坏载荷：忽略，下次 fetch 重新对齐
            }
            apply(notify.lexicon)
        }
    }

    fun stop() {
        unsubscribe?.invoke()
        unsubscribe = null
    }

    /**
     * 条件 GET。永不抛（超时/断线/非 OK 一律完成 false）：提案规定同步失败
     * 不上抛 UI——回退词库继续生效，下次连接再拉。
     */
    fun fetch(): CompletableFuture<Boolean> =
        conn.request(
            MsgSpecs.wordFilterFetch,
            Chat.WordFilterFetchRequest.newBuilder().setKnownVersion(version).build(),
            timeoutMs,
        )
            .thenApply { resp ->
                if (resp.code != Common.ErrorCode.OK) {
                    false
                } else {
                    apply(resp.lexicon)
                    true
                }
            }
            .exceptionally { false }

    /**
     * 本地文件回退：服务端格式的词库行（原 WordFilterLoader 的输入）。
     * 不动 [version]，首个服务端词库照常接管。
     */
    @JvmOverloads
    fun loadLocal(
        lines: List<String>,
        policy: WordFilterPolicy = WordFilterPolicy.REPLACE,
        replacement: String = "**",
    ) {
        inner = WordFilter(
            WordFilterOptions(
                terms = WordFilter.parseWordLexicon(lines),
                policy = policy,
                replacement = replacement,
            ),
        )
    }

    /** 无词库生效（服务端未启用或 record）时原样放行。 */
    override fun onBeforeSend(request: Chat.SendMessageRequest): Chat.SendMessageRequest? {
        val current = inner ?: return request
        val original = request.content.toStringUtf8()
        val result = current.filter(original)
        if (!result.allowed) return null // null = 拦截（管线转 RequestError.BLOCKED）
        if (result.content == original) return request
        return request.toBuilder()
            .setContent(ByteString.copyFromUtf8(result.content))
            .build()
    }

    private fun apply(lex: Chat.WordFilterLexicon) {
        if (lex.version <= version) return
        version = lex.version
        inner = if (!lex.enabled || lex.policy == Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_RECORD) {
            null
        } else {
            WordFilter(
                WordFilterOptions(
                    terms = WordFilter.parseWordLexicon(lex.lexicon.split('\n')),
                    policy = if (lex.policy == Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REJECT) {
                        WordFilterPolicy.REJECT
                    } else {
                        WordFilterPolicy.REPLACE
                    },
                    replacement = lex.replacement.ifEmpty { "**" },
                ),
            )
        }
    }

    private companion object {
        const val DEFAULT_TIMEOUT_MS = 10_000L
    }
}
