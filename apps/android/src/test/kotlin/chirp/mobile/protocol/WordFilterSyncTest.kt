package chirp.mobile.protocol

import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * 词库下发同步（WordFilterSync）的协议面测试：真实 ChatConnection +
 * FakeWsTransport，按线格式应答 FETCH_RESP / 投递 UPDATE_NOTIFY。
 */
class WordFilterSyncTest {
    private val scheduler = ManualScheduler(startMs = 1_000)
    private val transports = ArrayList<FakeWsTransport>()

    private fun connected(): Pair<ChatConnection, FakeWsTransport> {
        val connection = ChatConnection(
            url = "ws://test/chirp",
            transportFactory = {
                FakeWsTransport().also { transports.add(it) }
            },
            scheduler = scheduler,
        )
        connection.connect().get(5, TimeUnit.SECONDS)
        return connection to transports.last()
    }

    private fun wsMessage(
        msgId: Gateway.MsgID,
        sequence: Long,
        body: com.google.protobuf.MessageLite,
    ): ByteArray = encodeFrame(
        Gateway.Packet.newBuilder()
            .setMsgId(msgId)
            .setSequence(sequence)
            .setBody(body.toByteString())
            .build()
            .toByteArray(),
    )

    private fun lexicon(
        version: Long,
        enabled: Boolean,
        policy: Chat.WordFilterDeliveryPolicy,
        text: String,
    ): Chat.WordFilterLexicon = Chat.WordFilterLexicon.newBuilder()
        .setVersion(version)
        .setEnabled(enabled)
        .setPolicy(policy)
        .setReplacement("**")
        .setLexicon(text)
        .build()

    private fun run(sync: WordFilterSync, content: String): Pair<Boolean, String> {
        val request = Chat.SendMessageRequest.newBuilder()
            .setContent(ByteString.copyFromUtf8(content))
            .build()
        val effective = sync.onBeforeSend(request)
            ?: return false to content // null = 拦截（内容不上线）
        return true to effective.content.toStringUtf8()
    }

    private fun <T> java.util.concurrent.CompletableFuture<T>.settled(): T = get(5, TimeUnit.SECONDS)

    // ---- fetch -------------------------------------------------------------

    @Test
    fun fetchSendsKnownVersionAndAppliesTheServerLexicon() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        val future = sync.fetch()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.WORD_FILTER_FETCH_REQ, sent.msgId)
        assertEquals(
            0L,
            Chat.WordFilterFetchRequest.parseFrom(sent.body).knownVersion,
        )
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                sent.sequence,
                Chat.WordFilterFetchResponse.newBuilder()
                    .setCode(Common.ErrorCode.OK)
                    .setLexicon(
                        lexicon(3, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "spam\nbad\n"),
                    )
                    .build(),
            ),
        )
        assertTrue(future.settled())
        assertEquals(3L, sync.currentVersion)
        assertEquals(2, sync.wordCount)
        assertEquals(true to "hello **", run(sync, "hello spam"))
    }

    @Test
    fun conditionalGetSkipsTheRebuildWhenTheServerVersionMatches() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.fetch().let {
            transport.deliverWsMessage(
                wsMessage(
                    Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                    Gateway.Packet.parseFrom(transport.lastSentPayload()).sequence,
                    Chat.WordFilterFetchResponse.newBuilder()
                        .setCode(Common.ErrorCode.OK)
                        .setLexicon(lexicon(3, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "spam"))
                        .build(),
                ),
            )
            assertTrue(it.settled())
        }
        // 第二轮：同版本应答不带文本（条件 GET 命中），预检不动。
        val future = sync.fetch()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(3L, Chat.WordFilterFetchRequest.parseFrom(sent.body).knownVersion)
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                sent.sequence,
                Chat.WordFilterFetchResponse.newBuilder()
                    .setCode(Common.ErrorCode.OK)
                    .setLexicon(lexicon(3, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, ""))
                    .build(),
            ),
        )
        assertTrue(future.settled())
        assertEquals(3L, sync.currentVersion)
        assertEquals(1, sync.wordCount)
        assertEquals(true to "hello **", run(sync, "hello spam"))
    }

    @Test
    fun nonOkFetchCodeResolvesFalseAndAppliesNothing() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        val future = sync.fetch()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                sent.sequence,
                Chat.WordFilterFetchResponse.newBuilder()
                    .setCode(Common.ErrorCode.AUTH_FAILED)
                    .build(),
            ),
        )
        assertFalse(future.settled())
        assertEquals(0L, sync.currentVersion)
        assertEquals(0, sync.wordCount)
    }

    @Test
    fun fetchTimeoutResolvesFalseWithoutThrowing() {
        val (conn, _) = connected()
        val sync = WordFilterSync(conn, timeoutMs = 5_000)
        val future = sync.fetch()
        scheduler.advance(5_000)
        assertFalse(future.settled())
    }

    // ---- UPDATE_NOTIFY -----------------------------------------------------

    @Test
    fun notifyHotSwapsThePrecheck() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.start()
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(4, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "evil"))
                    .build(),
            ),
        )
        assertEquals(4L, sync.currentVersion)
        assertEquals(true to "you ** one", run(sync, "you evil one"))
    }

    @Test
    fun staleNotifiesAreIgnored() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.start()
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(5, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "fresh"))
                    .build(),
            ),
        )
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(2, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "stale"))
                    .build(),
            ),
        )
        assertEquals(5L, sync.currentVersion)
        assertEquals(true to "**", run(sync, "fresh"))
        assertEquals(true to "stale", run(sync, "stale"))
    }

    @Test
    fun serverWithoutLexiconClearsEvenTheLocalFallback() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.loadLocal(listOf("spam"))
        assertEquals(true to "hello **", run(sync, "hello spam"))
        sync.start()
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(1, false, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, ""))
                    .build(),
            ),
        )
        assertEquals(1L, sync.currentVersion)
        assertEquals(0, sync.wordCount)
        assertEquals(true to "hello spam", run(sync, "hello spam"))
    }

    @Test
    fun recordPolicyPassesThroughWithoutAPrecheck() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.start()
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(2, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_RECORD, "spam"))
                    .build(),
            ),
        )
        assertEquals(0, sync.wordCount)
        assertEquals(true to "hello spam", run(sync, "hello spam"))
    }

    @Test
    fun stopDetachesTheNotifySubscription() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.start()
        sync.stop()
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_UPDATE_NOTIFY,
                0,
                Chat.WordFilterUpdateNotify.newBuilder()
                    .setLexicon(lexicon(9, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "late"))
                    .build(),
            ),
        )
        assertEquals(0L, sync.currentVersion)
    }

    // ---- fallback & policy mirror -------------------------------------------

    @Test
    fun loadLocalIsTheFallbackUntilTheServerLexiconSupersedesIt() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        sync.loadLocal(listOf("localbad"), policy = WordFilterPolicy.REJECT)
        assertEquals(false to "x localbad", run(sync, "x localbad"))
        val future = sync.fetch()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                sent.sequence,
                Chat.WordFilterFetchResponse.newBuilder()
                    .setCode(Common.ErrorCode.OK)
                    .setLexicon(lexicon(1, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REPLACE, "serverbad"))
                    .build(),
            ),
        )
        assertTrue(future.settled())
        assertEquals(true to "x localbad", run(sync, "x localbad"))
        assertEquals(true to "x **", run(sync, "x serverbad"))
    }

    @Test
    fun rejectPolicyBlocksTheSendBeforeTheWire() {
        val (conn, transport) = connected()
        val sync = WordFilterSync(conn)
        val future = sync.fetch()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.WORD_FILTER_FETCH_RESP,
                sent.sequence,
                Chat.WordFilterFetchResponse.newBuilder()
                    .setCode(Common.ErrorCode.OK)
                    .setLexicon(lexicon(1, true, Chat.WordFilterDeliveryPolicy.WORD_FILTER_POLICY_REJECT, "spam"))
                    .build(),
            ),
        )
        assertTrue(future.settled())
        assertEquals(false to "hello spam", run(sync, "hello spam"))
        assertEquals(true to "clean", run(sync, "clean"))
    }
}
