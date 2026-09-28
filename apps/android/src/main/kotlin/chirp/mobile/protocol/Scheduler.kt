package chirp.mobile.protocol

import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

/** Handle for a scheduled task; cancel() before it fires drops it. */
fun interface Cancellable {
    fun cancel()
}

/**
 * Time seam for heartbeats, request deadlines and reconnect backoff (the
 * dart pipeline leans on the event-loop Timer; tests inject a manual
 * scheduler and drive the clock by hand).
 */
interface Scheduler {
    fun nowMs(): Long

    /** Run [task] once after [delayMs]. */
    fun post(delayMs: Long, task: () -> Unit): Cancellable

    /** Run [task] every [intervalMs] until cancelled (fixed-delay). */
    fun postPeriodic(intervalMs: Long, task: () -> Unit): Cancellable
}

/**
 * Production scheduler on a shared daemon single-thread pool (companion-wide:
 * one idle ticker thread for every connection in the process, ticks are cheap
 * state guards). Never shut down — connection lifecycles outlive any single
 * executor; disconnected connections simply no-op their ticks.
 */
class ExecutorScheduler :
    Scheduler {
    private val executor = ScheduledThreadPoolExecutor(1) { r ->
        Thread(r, "chirp-protocol-scheduler").apply { isDaemon = true }
    }

    init {
        executor.removeOnCancelPolicy = true
    }

    override fun nowMs(): Long = System.currentTimeMillis()

    override fun post(delayMs: Long, task: () -> Unit): Cancellable {
        val f = executor.schedule(task, delayMs, TimeUnit.MILLISECONDS)
        return Cancellable { f.cancel(false) }
    }

    override fun postPeriodic(intervalMs: Long, task: () -> Unit): Cancellable {
        val f = executor.scheduleWithFixedDelay(task, intervalMs, intervalMs, TimeUnit.MILLISECONDS)
        return Cancellable { f.cancel(false) }
    }
}
