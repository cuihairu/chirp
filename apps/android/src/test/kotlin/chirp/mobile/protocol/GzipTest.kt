package chirp.mobile.protocol

import chirp.chat.Chat
import chirp.common.Common
import com.google.protobuf.InvalidProtocolBufferException
import java.io.ByteArrayOutputStream
import java.util.zip.GZIPOutputStream
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/**
 * 消息压缩消费面：gzip 原语 + GET_HISTORY 双形态拆封 (game_chat_features P2)。
 * 与 C++/TS/C# 三端同形语义对拍。
 */
class GzipTest {
    private fun gzip(data: ByteArray): ByteArray {
        val output = ByteArrayOutputStream()
        GZIPOutputStream(output).use { it.write(data) }
        return output.toByteArray()
    }

    // ---- primitive ----

    @Test
    fun roundTripKeepsEveryByte() {
        val payload = ByteArray(4096) { (it * 31 + it / 7).toByte() }

        assertContentEquals(payload, gzipDecompress(gzip(payload)))
    }

    @Test
    fun roundTripSpansManyReads() {
        // 300k high-entropy bytes force many 8 KiB reads and a large container,
        // so a mid-stream byte flip lands inside the deflate body.
        val payload = Random(20261015).nextBytes(300_000)

        assertContentEquals(payload, gzipDecompress(gzip(payload)))
    }

    @Test
    fun emptyInputRaises() {
        assertFailsWith<GzipError> { gzipDecompress(ByteArray(0)) }
    }

    @Test
    fun corruptContainerRaises() {
        val container = gzip(ByteArray(300_000))
        container[container.size / 2] = (container[container.size / 2].toInt() xor 0xFF).toByte()

        assertFailsWith<GzipError> { gzipDecompress(container) }
    }

    @Test
    fun truncatedContainerRaises() {
        val container = gzip(ByteArray(300_000))

        assertFailsWith<GzipError> { gzipDecompress(container.copyOf(container.size / 2)) }
    }

    @Test
    fun headerOnlyContainerRaises() {
        // RFC 1952 header with no deflate body and no trailer.
        val container = byteArrayOf(0x1F, 0x8B.toByte(), 0x08, 0x00, 0, 0, 0, 0, 0, 0x03)

        assertFailsWith<GzipError> { gzipDecompress(container) }
    }

    @Test
    fun overCapRaises() {
        // 17 MiB of zeros compresses to a few KiB; inflation must abort at the
        // 16 MiB cap instead of buffering the whole body.
        assertFailsWith<GzipError> { gzipDecompress(gzip(ByteArray(17 * 1024 * 1024))) }
    }

    // ---- spec surface ----

    private fun decode(body: ByteArray): Chat.GetHistoryResponse =
        MsgSpecs.getHistory.decodeResponse(body)

    private fun gzipForm(inner: Chat.GetHistoryResponse): ByteArray =
        Chat.GetHistoryResponse.newBuilder()
            .setCode(Common.ErrorCode.OK)
            .setHasMore(true)
            .setMessagesGzip(
                com.google.protobuf.ByteString.copyFrom(gzip(inner.toByteArray())),
            )
            .build()
            .toByteArray()

    @Test
    fun getHistoryInflatesTheGzipForm() {
        val inner = Chat.GetHistoryResponse.newBuilder()
            .addMessages(Chat.ChatMessage.newBuilder().setMessageId("m1").setSenderId("u1"))
            .addMessages(Chat.ChatMessage.newBuilder().setMessageId("m2").setSenderId("u2"))
            .build()

        val resp = decode(gzipForm(inner))

        assertEquals(Common.ErrorCode.OK, resp.code)
        assertTrue(resp.hasMore)
        assertTrue(resp.messagesGzip.isEmpty)
        assertEquals(listOf("m1", "m2"), resp.messagesList.map { it.messageId })
        assertEquals(listOf("u1", "u2"), resp.messagesList.map { it.senderId })
    }

    @Test
    fun getHistoryAcceptsThePlainForm() {
        val resp = decode(
            Chat.GetHistoryResponse.newBuilder()
                .setCode(Common.ErrorCode.OK)
                .addMessages(Chat.ChatMessage.newBuilder().setMessageId("m9").setSenderId("u9"))
                .build()
                .toByteArray(),
        )

        assertEquals(listOf("m9"), resp.messagesList.map { it.messageId })
    }

    @Test
    fun getHistoryRejectsACorruptContainer() {
        val container = gzip(ByteArray(300_000))
        container[container.size / 2] = (container[container.size / 2].toInt() xor 0xFF).toByte()
        val body = Chat.GetHistoryResponse.newBuilder()
            .setMessagesGzip(com.google.protobuf.ByteString.copyFrom(container))
            .build()
            .toByteArray()

        assertFailsWith<GzipError> { decode(body) }
    }

    @Test
    fun getHistoryRejectsAGarbagePayload() {
        // A valid gzip container whose payload is not a GetHistoryResponse:
        // proto parsing must fail, the same BadResponse-equivalent outcome the
        // other three runtimes report for the identical case.
        val body = Chat.GetHistoryResponse.newBuilder()
            .setMessagesGzip(
                com.google.protobuf.ByteString.copyFrom(
                    gzip(byteArrayOf(0xFF.toByte(), 0xFE.toByte(), 0xFD.toByte(), 0xFC.toByte())),
                ),
            )
            .build()
            .toByteArray()

        assertFailsWith<InvalidProtocolBufferException> { decode(body) }
    }
}