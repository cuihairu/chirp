package chirp.mobile.protocol

import chirp.chat.Chat
import com.google.protobuf.ByteString
import java.io.StringReader
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class HooksTest {
    private fun message(
        content: String,
        channelType: Chat.ChannelType = Chat.ChannelType.PRIVATE,
        channelId: String = "a|b",
        timestamp: Long = 5_000,
    ): Chat.ChatMessage = Chat.ChatMessage.newBuilder()
        .setSenderId("alice")
        .setChannelType(channelType)
        .setChannelId(channelId)
        .setMsgType(Chat.MsgType.TEXT)
        .setContent(ByteString.copyFromUtf8(content))
        .setTimestamp(timestamp)
        .build()

    // ---- MemoryMessageStore -------------------------------------------------

    @Test
    fun storeLoadsNewestFirstAndEvictsOldestBeyondCap() {
        val store = MemoryMessageStore(maxPerChannel = 2)
        store.save(message("one", timestamp = 1))
        store.save(message("two", timestamp = 2))
        store.save(message("three", timestamp = 3))
        val page = store.load(Chat.ChannelType.PRIVATE, "a|b", 10)
        assertEquals(listOf("three", "two"), page.map { it.content.toStringUtf8() })
    }

    @Test
    fun storeKeysPerChannelTypeAndChannelId() {
        val store = MemoryMessageStore()
        store.save(message("private", channelId = "a|b"))
        store.save(message("world", channelType = Chat.ChannelType.WORLD, channelId = "world"))
        assertEquals(1, store.load(Chat.ChannelType.PRIVATE, "a|b", 10).size)
        assertEquals(1, store.load(Chat.ChannelType.WORLD, "world", 10).size)
        assertEquals(0, store.load(Chat.ChannelType.PRIVATE, "world", 10).size)
    }

    @Test
    fun storeHonorsLimitAndBeforeTimestamp() {
        val store = MemoryMessageStore()
        store.save(message("one", timestamp = 1))
        store.save(message("two", timestamp = 2))
        store.save(message("three", timestamp = 3))
        assertEquals(listOf("three", "two"), store.load(Chat.ChannelType.PRIVATE, "a|b", 2).map { it.content.toStringUtf8() })
        assertEquals(
            listOf("two", "one"),
            store.load(Chat.ChannelType.PRIVATE, "a|b", 10, beforeTimestamp = 3).map { it.content.toStringUtf8() },
        )
        assertEquals(0, store.load(Chat.ChannelType.PRIVATE, "a|b", 0).size)
    }

    @Test
    fun storeCleanupDropsOldEntriesAndEmptyChannels() {
        val store = MemoryMessageStore()
        store.save(message("old-private", channelId = "a|b", timestamp = 10))
        store.save(message("old-world", channelType = Chat.ChannelType.WORLD, channelId = "world", timestamp = 20))
        store.save(message("fresh", channelId = "a|b", timestamp = 500))
        store.cleanup(olderThanMs = 100)
        assertEquals(listOf("fresh"), store.load(Chat.ChannelType.PRIVATE, "a|b", 10).map { it.content.toStringUtf8() })
        assertEquals(0, store.load(Chat.ChannelType.WORLD, "world", 10).size)
    }

    // ---- WordFilterLoader ---------------------------------------------------

    @Test
    fun loaderReadsServerFormatLexiconFromAReader() {
        val filter = WordFilterLoader.load(
            StringReader("Spam\r\n# 注释行\n\n  dummy \n论坛\n"),
        )
        assertEquals(listOf("dummy", "spam", "论坛"), filter.terms)
        val result = filter.filter("hey SPAM now")
        assertTrue(result.allowed)
        assertEquals("hey ** now", result.content)
    }

    @Test
    fun loaderHonorsRejectPolicy() {
        val filter = WordFilterLoader.load(StringReader("banned\n"), policy = WordFilterPolicy.REJECT)
        assertTrue(!filter.filter("totally BANNED words").allowed)
    }
}
