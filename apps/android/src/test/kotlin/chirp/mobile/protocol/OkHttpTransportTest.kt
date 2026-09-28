package chirp.mobile.protocol

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import okhttp3.OkHttpClient
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okhttp3.WebSocket
import okhttp3.WebSocketListener

/**
 * Real-socket tests for the OkHttp adapter over MockWebServer's WebSocket
 * upgrade (the only non-fake surface in the gate; everything above the
 * transport is deterministic on the fake).
 */
class OkHttpTransportTest {
    private fun newTransport(server: MockWebServer): OkHttpTransport =
        OkHttpTransport(
            url = server.url("/ws").toString(),
            client = OkHttpClient.Builder()
                .connectTimeout(5, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS)
                .build(),
        )

    /** Open with a timeout; returns the transport. */
    private fun open(transport: OkHttpTransport) {
        val done = CountDownLatch(1)
        var failure: Throwable? = null
        transport.open { err ->
            failure = err
            done.countDown()
        }
        assertTrue(done.await(10, TimeUnit.SECONDS), "open did not complete")
        assertNull(failure, "open failed: $failure")
    }

    @Test
    fun openCompletesAndBinaryMessagesRoundTripThenClosedFiresExactlyOnce() {
        val server = MockWebServer()
        val serverReceived = ArrayBlockingQueueLookalike()
        server.enqueue(
            MockResponse().withWebSocketUpgrade(object : WebSocketListener() {
                override fun onMessage(ws: WebSocket, bytes: okio.ByteString) {
                    serverReceived.add(bytes.toByteArray())
                    ws.send(bytes) // echo, then a clean server-side close
                    ws.close(1000, null)
                }
            }),
        )
        server.start()
        val client = OkHttpClient.Builder()
            .connectTimeout(5, TimeUnit.SECONDS)
            .readTimeout(5, TimeUnit.SECONDS)
            .build()
        try {
            val transport = OkHttpTransport(url = server.url("/ws").toString(), client = client)
            val received = ArrayBlockingQueueLookalike()
            val closedCount = java.util.concurrent.atomic.AtomicInteger()
            transport.onBinary { received.add(it) }
            transport.onClosed { closedCount.incrementAndGet() }
            open(transport)

            transport.send(encodeFrame(byteArrayOf(1, 2, 3)))
            val echoed = assertNotNull(received.poll(10), "no echoed message")
            // The echo is the same frame we sent (transport moves raw bytes).
            assertContentEquals(encodeFrame(byteArrayOf(1, 2, 3)), echoed)
            assertContentEquals(encodeFrame(byteArrayOf(1, 2, 3)), serverReceived.poll(10))

            // The server close must surface as exactly one down event…
            val deadline = System.nanoTime() + 10_000_000_000L
            while (closedCount.get() == 0 && System.nanoTime() < deadline) Thread.sleep(20)
            assertEquals(1, closedCount.get(), "closed did not fire after the server closed")
            // …and a close() after the fact must not announce a second time.
            transport.close()
            Thread.sleep(200)
            assertEquals(1, closedCount.get(), "closed announced twice")
        } finally {
            // Release the client's threads before the server: shutdown()
            // otherwise gives up waiting on the still-pooled connection.
            client.dispatcher.executorService.shutdown()
            client.connectionPool.evictAll()
            // MockWebServer's queue teardown is time-based and can give up on
            // a websocket test even when both ends closed cleanly; the
            // functional assertions above are the gate, so shutdown stays
            // best-effort here.
            runCatching { server.shutdown() }
        }
    }

    @Test
    fun openFailureIsReportedThroughOpenAndNotThroughClosed() {
        val server = MockWebServer()
        server.enqueue(MockResponse().setResponseCode(404)) // no upgrade
        server.start()
        try {
            val transport = newTransport(server)
            var closedFired = false
            transport.onClosed { closedFired = true }
            val done = CountDownLatch(1)
            var failure: Throwable? = null
            transport.open { err ->
                failure = err
                done.countDown()
            }
            assertTrue(done.await(10, TimeUnit.SECONDS), "open did not complete")
            assertNotNull(failure, "the failed upgrade must surface as an open error")
            // Dart contract: after an open error the connection runs its own
            // close path, so the down handler must not fire either.
            Thread.sleep(200)
            assertFalse(closedFired, "closed must not fire after an open failure")
        } finally {
            server.shutdown()
        }
    }

    @Test
    fun textFramesAreDroppedBinaryFramesAreDelivered() {
        val server = MockWebServer()
        server.enqueue(
            MockResponse().withWebSocketUpgrade(object : WebSocketListener() {
                override fun onOpen(ws: WebSocket, response: okhttp3.Response) {
                    ws.send("not part of the protocol")
                    ws.send(okio.ByteString.of(9, 9))
                }
            }),
        )
        server.start()
        try {
            val transport = newTransport(server)
            val received = ArrayBlockingQueueLookalike()
            transport.onBinary { received.add(it) }
            open(transport)
            val message = assertNotNull(received.poll(10), "binary frame missing")
            assertContentEquals(byteArrayOf(9, 9), message)
        } finally {
            server.shutdown()
        }
    }
}

/** Tiny blocking queue for cross-thread handoff in tests. */
private class ArrayBlockingQueueLookalike {
    private val lock = Object()
    private val items = ArrayDeque<ByteArray>()

    fun add(item: ByteArray) {
        synchronized(lock) {
            items.addLast(item)
            lock.notifyAll()
        }
    }

    /** Poll with a timeout in seconds; null on expiry. */
    fun poll(timeoutSeconds: Long): ByteArray? {
        val deadline = System.nanoTime() + timeoutSeconds * 1_000_000_000L
        synchronized(lock) {
            while (items.isEmpty()) {
                val rest = deadline - System.nanoTime()
                if (rest <= 0) return null
                lock.wait(rest / 1_000_000L, (rest % 1_000_000L).toInt())
            }
            return items.removeFirst()
        }
    }
}
