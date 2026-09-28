package chirp.mobile.protocol

import chirp.chat.Chat
import chirp.common.Common
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class OfflineSendQueueTest {
    private val privatePeer = SendOptions(Chat.ChannelType.PRIVATE, receiverId = "alice")

    private fun okResponse(): Chat.SendMessageResponse =
        Chat.SendMessageResponse.newBuilder().setCode(Common.ErrorCode.OK).build()

    @Test
    fun enqueueDedupesByClientIdAndEvictsOldestBeyondCap() {
        var now = 1_000L
        val queue = OfflineSendQueue(
            send = { _, _ -> CompletableFuture.completedFuture(okResponse()) },
            clock = { ++now },
            maxQueued = 2,
        )
        assertTrue(queue.enqueue("c1", privatePeer, "one"))
        assertFalse(queue.enqueue("c1", privatePeer, "one-dup"))
        assertTrue(queue.enqueue("c2", privatePeer, "two"))
        assertTrue(queue.enqueue("c3", privatePeer, "three")) // evicts c1

        assertEquals(listOf("c2", "c3"), queue.pending().map { it.clientId })
        assertEquals("two", queue.pending().first().content)
        assertEquals(1_002L, queue.pending().first().queuedAtMs)
    }

    @Test
    fun flushReplaysInOrderAndCountsConfirmedSends() {
        val sentContents = ArrayList<String>()
        val queue = OfflineSendQueue(
            send = { _, content ->
                sentContents.add(content)
                CompletableFuture.completedFuture(okResponse())
            },
        )
        queue.enqueue("c1", privatePeer, "one")
        queue.enqueue("c2", privatePeer, "two")
        assertEquals(2, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(listOf("one", "two"), sentContents)
        assertEquals(0, queue.size())
    }

    @Test
    fun flushStopsAtStillOfflineAndKeepsTheTailQueued() {
        val calls = ArrayList<String>()
        val queue = OfflineSendQueue(
            send = { _, content ->
                calls.add(content)
                CompletableFuture.failedFuture(
                    RequestError(RequestError.Kind.CLOSED),
                )
            },
        )
        queue.enqueue("c1", privatePeer, "one")
        queue.enqueue("c2", privatePeer, "two")
        assertEquals(0, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(listOf("one"), calls) // first CLOSED stops the batch
        assertEquals(listOf("c1", "c2"), queue.pending().map { it.clientId })
    }

    @Test
    fun locallyRejectedEntriesAreDroppedNotRetried() {
        val queue = OfflineSendQueue(
            send = { _, _ ->
                CompletableFuture.failedFuture(
                    RequestError(RequestError.Kind.BLOCKED, message = "interceptor"),
                )
            },
        )
        queue.enqueue("c1", privatePeer, "one")
        assertEquals(0, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(0, queue.size())
    }

    @Test
    fun argumentErrorsCountAsRejected() {
        val queue = OfflineSendQueue(
            send = { _, _ -> CompletableFuture.failedFuture(IllegalArgumentException("no receiver")) },
        )
        queue.enqueue("c1", privatePeer, "one")
        queue.flush().get(5, TimeUnit.SECONDS)
        assertEquals(0, queue.size())
    }

    @Test
    fun timeoutKeepsTheEntryForAtLeastOnceReplay() {
        val queue = OfflineSendQueue(
            send = { _, _ ->
                CompletableFuture.failedFuture(RequestError(RequestError.Kind.TIMEOUT))
            },
        )
        queue.enqueue("c1", privatePeer, "one")
        assertEquals(0, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(1, queue.size())
    }

    @Test
    fun anyServerResponseIncludingTargetOfflineConfirmsTheEntry() {
        val queue = OfflineSendQueue(
            send = { _, _ ->
                CompletableFuture.completedFuture(
                    Chat.SendMessageResponse.newBuilder()
                        .setCode(Common.ErrorCode.TARGET_OFFLINE)
                        .build(),
                )
            },
        )
        queue.enqueue("c1", privatePeer, "one")
        assertEquals(1, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(0, queue.size())
    }

    @Test
    fun emptyFlushIsZeroAndDoesNotCallSend() {
        var calls = 0
        val queue = OfflineSendQueue(send = { _, _ -> calls++; CompletableFuture.completedFuture(okResponse()) })
        assertEquals(0, queue.flush().get(5, TimeUnit.SECONDS))
        assertEquals(0, calls)
    }

    @Test
    fun clearEmptiesTheQueue() {
        val queue = OfflineSendQueue(send = { _, _ -> CompletableFuture.completedFuture(okResponse()) })
        queue.enqueue("c1", privatePeer, "one")
        queue.clear()
        assertEquals(0, queue.size())
    }
}
