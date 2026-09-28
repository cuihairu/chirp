package chirp.mobile.protocol

import chirp.chat.Chat
import java.util.concurrent.CompletableFuture

/**
 * Client outbox for sends attempted while the link is down: entries are
 * replayed in order on the next reconnect (wire `flush()` to
 * ChatEventListener.onReconnected). The queue itself knows nothing about the
 * connection — it drives any send function, which keeps it fully testable on
 * a fake and lets the shell decide the wiring.
 *
 * Semantics (deliberate, documented):
 * - **at-least-once**: a [RequestError.Kind.TIMEOUT] keeps the entry queued —
     the send may or may not have reached the server, so replay may
 *   duplicate; dedupe by clientId downstream if that matters.
 * - [RequestError.Kind.CLOSED] (still offline) stops the flush at that
 *   entry; the tail stays queued in order.
 * - [RequestError.Kind.BLOCKED] / [IllegalArgumentException] remove the
 *   entry: locally rejected messages must not retry forever.
 * - Any server response removes the entry — TARGET_OFFLINE counts as sent
 *   (the server holds the message for the recipient), matching dart chat_api.
 * - Dedupe by clientId at enqueue (a resend double-tap must not double-queue);
 *   when full the **oldest** entry is evicted (newest wins, bounded memory).
 */
class OfflineSendQueue(
    private val send: (SendOptions, String) -> CompletableFuture<Chat.SendMessageResponse>,
    private val clock: () -> Long = System::currentTimeMillis,
    private val maxQueued: Int = 50,
) {
    data class Entry(
        val clientId: String,
        val options: SendOptions,
        val content: String,
        val queuedAtMs: Long,
    )

    private val lock = Any()
    private val queue = ArrayDeque<Entry>()

    /** false = a client with this id is already queued (duplicate ignored). */
    fun enqueue(clientId: String, options: SendOptions, content: String): Boolean = synchronized(lock) {
        if (queue.any { it.clientId == clientId }) return false
        if (queue.size >= maxQueued) queue.removeFirst()
        queue.addLast(Entry(clientId, options, content, clock()))
        true
    }

    fun size(): Int = synchronized(lock) { queue.size }

    fun pending(): List<Entry> = synchronized(lock) { queue.toList() }

    fun clear() = synchronized(lock) { queue.clear() }

    /**
     * Replays the queue oldest-first until the link is down again or the
     * queue drains. Completes with the number of entries confirmed sent (a
     * server response of any code, incl. TARGET_OFFLINE) or locally rejected
     * (dropped). Empty queue → 0.
     */
    fun flush(): CompletableFuture<Int> {
        val batch = synchronized(lock) { queue.toList() }
        if (batch.isEmpty()) return CompletableFuture.completedFuture(0)
        var confirmed = 0
        var aborted = false
        var chain: CompletableFuture<*> = CompletableFuture.completedFuture<Any?>(null)
        for (entry in batch) {
            chain = chain.thenCompose {
                if (aborted) {
                    CompletableFuture.completedFuture(Outcome.OFFLINE)
                } else {
                    send(entry.options, entry.content).handle { resp, err ->
                        val verdict = classify(err)
                        when (verdict) {
                            Outcome.SENT -> {
                                confirmed++
                                drop(entry.clientId)
                            }
                            Outcome.REJECTED -> drop(entry.clientId)
                            Outcome.OFFLINE -> {
                                // Link is down again: stop the batch here and
                                // keep this entry plus the tail queued in order.
                                aborted = true
                            }
                        }
                        verdict
                    }
                }
            }
        }
        return chain.thenApply { confirmed }
    }

    private enum class Outcome { SENT, REJECTED, OFFLINE }

    private fun classify(err: Throwable?): Outcome = when {
        err == null -> Outcome.SENT
        err is RequestError && err.kind == RequestError.Kind.CLOSED -> Outcome.OFFLINE
        err is RequestError && err.kind == RequestError.Kind.TIMEOUT -> Outcome.OFFLINE
        else -> Outcome.REJECTED
    }

    private fun drop(clientId: String) {
        synchronized(lock) {
            queue.removeAll { it.clientId == clientId }
        }
    }
}
