package chirp.mobile.protocol

import java.util.PriorityQueue
import kotlin.random.Random

/**
 * Deterministic virtual clock for tests: advance() runs due tasks in time
 * order on the calling thread; tasks posted during an advance with a due
 * time still inside the window fire too. Not thread-safe (test-only).
 */
class ManualScheduler(startMs: Long = 0L) : Scheduler {
    private class Task(val dueMs: Long, val seq: Long, val body: () -> Unit)

    private val queue = PriorityQueue<Task>(compareBy({ it.dueMs }, { it.seq }))
    private val cancelled = HashSet<Long>()
    private var clock: Long = startMs
    private var ids = 0L
    private var running = false

    override fun nowMs(): Long = clock

    private fun post0(delayMs: Long, task: () -> Unit): Cancellable {
        val id = ++ids
        queue.add(Task(clock + delayMs, id, task))
        return Cancellable { cancelled.add(id) }
    }

    override fun post(delayMs: Long, task: () -> Unit): Cancellable = post0(delayMs, task)

    override fun postPeriodic(intervalMs: Long, task: () -> Unit): Cancellable {
        // One step queued at a time; the step re-queues itself when it fires,
        // so cancelling the returned handle stops the whole chain.
        return post0(intervalMs) { step(intervalMs, task) }
    }

    private fun step(intervalMs: Long, task: () -> Unit) {
        task()
        post0(intervalMs) { step(intervalMs, task) }
    }

    /** Advance the virtual clock, firing every due (and not cancelled) task. */
    fun advance(ms: Long) {
        check(!running) { "advance() is not reentrant" }
        running = true
        try {
            val target = clock + ms
            while (true) {
                val t = queue.peek() ?: break
                if (t.dueMs > target) break
                queue.poll()
                if (t.seq in cancelled) continue
                clock = t.dueMs
                t.body()
            }
            clock = target
        } finally {
            running = false
        }
    }

    /** Queued, not-yet-cancelled task count (reconnect timers, heartbeats…). */
    fun pendingTasks(): Int = queue.count { it.seq !in cancelled }
}

/**
 * Random whose nextLong(bound) returns the bound's midpoint. The reconnect
 * delay is base + nextLong(2·jitter+1) − jitter, so the midpoint makes the
 * jitter cancel and the scheduled delay equal base exactly at every rung of
 * the ladder.
 */
open class FakeRandom : Random() {
    override fun nextBits(bitCount: Int): Int = 0
    override fun nextLong(until: Long): Long = if (until > 0) (until - 1) / 2 else 0L
}

/** Scripted [WsTransport] double: captures sends, pushes inbound messages. */
class FakeWsTransport : WsTransport {
    val sent = ArrayList<ByteArray>()
    var closeCount = 0
        private set
    var openAnnounced: ((Throwable?) -> Unit)? = null
        private set
    var binaryHandler: ((ByteArray) -> Unit)? = null
        private set
    var closedHandler: (() -> Unit)? = null
        private set

    /** null = open succeeds; anything else = open fails with it. */
    var failOpenWith: Throwable? = null

    /** false = open() only records the callback; the test announces later. */
    var autoOpen = true

    override fun open(onResult: (Throwable?) -> Unit) {
        openAnnounced = onResult
        if (autoOpen) {
            val err = failOpenWith
            onResult(err)
        }
    }

    override fun onBinary(handler: (ByteArray) -> Unit) {
        binaryHandler = handler
    }

    override fun onClosed(handler: () -> Unit) {
        closedHandler = handler
    }

    override fun send(data: ByteArray): Boolean {
        sent.add(data)
        return true
    }

    override fun close() {
        closeCount++
        // Mirrors IoWebSocket: closing announces the down event exactly once.
        closedHandler?.invoke()
    }

    /** Deliver one inbound WebSocket binary message (raw bytes). */
    fun deliverWsMessage(bytes: ByteArray) {
        binaryHandler!!.invoke(bytes)
    }

    /** The last outgoing frame's inner payload (u32 length prefix stripped). */
    fun lastSentPayload(): ByteArray {
        val frame = sent.last()
        val length = ((frame[0].toInt() and 0xFF) shl 24) or
            ((frame[1].toInt() and 0xFF) shl 16) or
            ((frame[2].toInt() and 0xFF) shl 8) or
            (frame[3].toInt() and 0xFF)
        return frame.copyOfRange(4, 4 + length)
    }
}
