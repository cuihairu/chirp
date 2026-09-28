package chirp.mobile.protocol

import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import com.google.protobuf.MessageLite
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.roundToLong
import kotlin.random.Random

/**
 * Connection state machine (port of mobile_companion chirp_client.dart):
 *   idle → connecting → connected ⇄ waitingReconnect (auto reconnect)
 *                     ↘ kicked        (KICK_NOTIFY: terminal for auto
 *                                      reconnect; an explicit connect() is a
 *                                      fresh user action and restarts)
 *   any  → closed        (manual disconnect(); terminal until connect())
 */
enum class ConnState { IDLE, CONNECTING, CONNECTED, WAITING_RECONNECT, CLOSED, KICKED }

/**
 * Tunables, mirroring ChirpClientOptions defaults from the dart pipeline:
 * the server has no idle kick — 25s pings keep intermediaries honest; 2
 * consecutive pings without any pong mean the link is dead in practice.
 */
data class ChirpClientOptions(
    val heartbeatIntervalMs: Long = 25_000,
    val maxMissedPongs: Int = 2,
    val requestTimeoutMs: Long = 10_000,
    val reconnectBaseMs: Long = 500,
    val reconnectMaxMs: Long = 15_000,
    /** ± fraction applied to each reconnect delay. */
    val jitterRatio: Double = 0.2,
)

private class PendingRequest(
    val raw: CompletableFuture<ByteArray>,
    val timeout: Cancellable,
)

/**
 * Typed websocket client for the Packet protocol: u32 framing, request/
 * response correlation by sequence, request deadlines, heartbeats with
 * missed-pong detection, auto reconnect with jittered exponential backoff and
 * terminal kick handling. Faithful port of the dart ChirpClient; the typed
 * [MessageSpec] round-trip API is the Kotlin-shaped surface over dart's
 * decode-at-call-site request.
 *
 * Dart runs everything on one event loop; here all state transitions hold
 * [lock] (transport callbacks arrive on OkHttp threads, timers on the
 * scheduler thread). [Scheduler] and [Random] are injectable so tests drive
 * time and jitter deterministically.
 */
class ChatConnection(
    val url: String,
    private val transportFactory: (String) -> WsTransport,
    val options: ChirpClientOptions = ChirpClientOptions(),
    private val random: Random = Random.Default,
    private val scheduler: Scheduler = SHARED_SCHEDULER,
) {
    private val lock = Any()
    private val decoder = FrameDecoder()
    private val seqCounter = AtomicLong(0)
    private val pending = ConcurrentHashMap<Long, PendingRequest>()
    private val notifyListeners =
        ConcurrentHashMap<Gateway.MsgID, CopyOnWriteArrayList<(ByteArray) -> Unit>>()
    private val stateListeners = CopyOnWriteArrayList<(ConnState) -> Unit>()
    private val reconnectListeners = CopyOnWriteArrayList<(attempt: Int, delayMs: Long) -> Unit>()
    private val reconnectedListeners = CopyOnWriteArrayList<() -> Unit>()

    @Volatile
    var state: ConnState = ConnState.IDLE
        private set

    /** True after the server sent KICK_NOTIFY; auto reconnect stays off. */
    @Volatile
    var kicked: Boolean = false
        private set

    /** server_time − local_time at the last pong; null before the first. */
    var clockOffsetMs: Long? = null
        private set

    private var ws: WsTransport? = null
    private var attempt = 0
    private var missedPongs = 0
    private var reconnectCancellable: Cancellable? = null
    private var heartbeatCancellable: Cancellable? = null

    /**
     * Opens the socket. Completes when open, errors if it closes/fails
     * first. An explicit connect() is a user action: it clears kick state and
     * cancels any pending reconnect timer.
     */
    fun connect(): CompletableFuture<Unit> {
        val result = CompletableFuture<Unit>()
        val t: WsTransport = synchronized(lock) {
            kicked = false
            cancelReconnectTimer()
            val stale = ws
            ws = null
            stale?.close()
            setStatus(ConnState.CONNECTING)
            val fresh = transportFactory(url)
            ws = fresh
            fresh.onBinary { bytes -> handleData(fresh, bytes) }
            fresh.onClosed { handleTransportClosed(fresh) }
            fresh
        }
        t.open { err ->
            synchronized(lock) {
                if (ws !== t) {
                    // disconnect() or a newer connect() raced the open: this
                    // attempt is superseded, so just settle its future.
                    result.completeExceptionally(RequestError(RequestError.Kind.CLOSED, message = "connect superseded"))
                    return@synchronized
                }
                if (err != null) {
                    result.completeExceptionally(err)
                } else {
                    missedPongs = 0
                    setStatus(ConnState.CONNECTED)
                    startHeartbeat()
                    // Observational only: a backoff counter > 0 means this
                    // open is a reconnect. resetBackoff() still owns the
                    // counter reset.
                    if (attempt > 0) reconnectedListeners.forEach { it() }
                    result.complete(Unit)
                }
            }
            if (err != null) handleTransportClosed(t)
        }
        return result
    }

    /** Manual close; no reconnect follows. */
    fun disconnect() = synchronized(lock) {
        setStatus(ConnState.CLOSED)
        clearHeartbeat()
        rejectAllPending(RequestError.Kind.CLOSED)
        cancelReconnectTimer()
        ws?.close()
        ws = null
    }

    /**
     * Reset the reconnect backoff after a successful login. Opening the
     * socket alone proves little (the server may still refuse the token), so
     * the api layer calls this once the LOGIN round-trip succeeded.
     */
    fun resetBackoff() = synchronized(lock) {
        attempt = 0
    }

    /** Immediate heartbeat (mobile app lifecycle resume). */
    fun heartbeatNow() = synchronized(lock) {
        if (state == ConnState.CONNECTED) pingTick()
    }

    /**
     * Typed request/response. Fails with [RequestError.Kind.CLOSED] when not
     * connected, [RequestError.Kind.TIMEOUT] on deadline; server error codes
     * are the caller's business (the decoded response completes normally).
     */
    fun <T : MessageLite> request(
        spec: MessageSpec<T>,
        body: MessageLite,
        timeoutMs: Long = options.requestTimeoutMs,
    ): CompletableFuture<T> {
        val raw = CompletableFuture<ByteArray>()
        synchronized(lock) {
            if (state != ConnState.CONNECTED || ws == null) {
                return CompletableFuture.failedFuture(RequestError(RequestError.Kind.CLOSED))
            }
            val seq = seqCounter.incrementAndGet()
            val timeoutHandle = scheduler.post(timeoutMs) {
                synchronized(lock) {
                    if (pending.remove(seq) != null) {
                        raw.completeExceptionally(RequestError(RequestError.Kind.TIMEOUT))
                    }
                }
            }
            pending[seq] = PendingRequest(raw, timeoutHandle)
            try {
                sendRaw(spec.reqMsgId, body.toByteString(), seq)
            } catch (e: Exception) {
                timeoutHandle.cancel()
                pending.remove(seq)
                raw.completeExceptionally(if (e is RequestError) e else RequestError(RequestError.Kind.CLOSED))
            }
        }
        return raw.thenApply { bytes -> spec.decodeResponse(bytes) }
    }

    /**
     * Fire-and-forget send for messages that never get a response frame
     * (MESSAGE_ACK 2209, TYPING 2208). Throws [RequestError] CLOSED when not
     * connected.
     */
    fun send(msgId: Gateway.MsgID, body: ByteArray) = synchronized(lock) {
        if (state != ConnState.CONNECTED || ws == null) {
            throw RequestError(RequestError.Kind.CLOSED)
        }
        sendRaw(msgId, ByteString.copyFrom(body))
    }

    /** Subscribe to server pushes (sequence === 0 packets). Returns unsubscribe. */
    fun onNotify(msgId: Gateway.MsgID, handler: (ByteArray) -> Unit): () -> Unit {
        val list = notifyListeners.computeIfAbsent(msgId) { CopyOnWriteArrayList() }
        list.add(handler)
        return { list.remove(handler) }
    }

    /** Subscribe to status flips. Returns unsubscribe. */
    fun onStateChange(handler: (ConnState) -> Unit): () -> Unit {
        stateListeners.add(handler)
        return { stateListeners.remove(handler) }
    }

    /**
     * A backoff reconnect is about to fire: 1-based attempt and the delay it
     * was scheduled with (jitter included). Only fires after the first drop.
     * Returns unsubscribe.
     */
    fun onReconnecting(listener: (attempt: Int, delayMs: Long) -> Unit): () -> Unit {
        reconnectListeners.add(listener)
        return { reconnectListeners.remove(listener) }
    }

    /**
     * A reconnect attempt reached 'connected' again. The backoff counter is
     * only reset by [resetBackoff] (login success) — this event is purely
     * observational, so the retry cadence is unchanged. Returns unsubscribe.
     */
    fun onReconnected(listener: () -> Unit): () -> Unit {
        reconnectedListeners.add(listener)
        return { reconnectedListeners.remove(listener) }
    }

    // ---- internals: all take the lock (dart's event-loop serialization) ----

    private fun setStatus(status: ConnState) {
        // Caller holds the lock.
        if (state == status) return
        state = status
        stateListeners.forEach { it(status) }
    }

    private fun startHeartbeat() {
        // Caller holds the lock.
        clearHeartbeat()
        heartbeatCancellable = scheduler.postPeriodic(options.heartbeatIntervalMs) { pingTick() }
    }

    private fun clearHeartbeat() {
        // Caller holds the lock.
        heartbeatCancellable?.cancel()
        heartbeatCancellable = null
    }

    private fun pingTick() {
        synchronized(lock) {
            if (state != ConnState.CONNECTED) return
            if (missedPongs >= options.maxMissedPongs) {
                // Consecutive missed pongs: the link is dead in practice —
                // close it and let the reconnect path take over.
                ws?.close()
                return
            }
            missedPongs++
            val ping = Gateway.HeartbeatPing.newBuilder().setTimestamp(scheduler.nowMs()).build()
            try {
                sendRaw(Gateway.MsgID.HEARTBEAT_PING, ping.toByteString())
            } catch (_: Exception) {
                // Socket died underneath us; the closed event will fire.
            }
        }
    }

    private fun sendRaw(
        msgId: Gateway.MsgID,
        body: ByteString,
        seq: Long = seqCounter.incrementAndGet(),
    ) {
        // Caller holds the lock.
        val t = ws ?: throw RequestError(RequestError.Kind.CLOSED)
        val packet = Gateway.Packet.newBuilder()
            .setMsgId(msgId)
            .setSequence(seq)
            .setBody(body)
            .build()
        if (!t.send(encodeFrame(packet.toByteArray()))) {
            throw RequestError(RequestError.Kind.CLOSED)
        }
    }

    private fun handleData(t: WsTransport, bytes: ByteArray) {
        synchronized(lock) {
            if (ws !== t) return  // stale transport from a superseded attempt
            val frames = try {
                decoder.feed(bytes)
            } catch (e: FrameError) {
                // A corrupted stream cannot be resynchronized; treat it as a
                // dead link (the close event drives the reconnect path).
                ws?.close()
                return
            }
            for (frame in frames) handleFrame(frame)
        }
    }

    private fun handleFrame(frame: ByteArray) {
        // Caller holds the lock.
        val packet = try {
            Gateway.Packet.parseFrom(frame)
        } catch (_: Exception) {
            return  // undecodable packet: ignore rather than kill the connection
        }
        if (packet.sequence == 0L) {
            dispatchNotify(packet)
            return
        }
        if (packet.msgId == Gateway.MsgID.HEARTBEAT_PONG) {
            // Pong echoes the ping's sequence and is not in the pending table.
            onPong(packet.body)
            return
        }
        val entry = pending.remove(packet.sequence) ?: return // response to an already timed-out request
        entry.timeout.cancel()
        entry.raw.complete(packet.body.toByteArray())
    }

    private fun onPong(body: ByteString) {
        missedPongs = 0
        try {
            val pong = Gateway.HeartbeatPong.parseFrom(body.toByteArray())
            clockOffsetMs = pong.serverTime - scheduler.nowMs()
        } catch (_: Exception) {
            // A malformed pong still proves the link is alive.
        }
    }

    private fun dispatchNotify(packet: Gateway.Packet) {
        // Caller holds the lock.
        if (packet.msgId == Gateway.MsgID.KICK_NOTIFY) {
            markKicked()
            // Fall through: subscribers may also want the KickNotify reason body.
        }
        notifyListeners[packet.msgId]?.forEach { handler ->
            try {
                handler(packet.body.toByteArray())
            } catch (_: Exception) {
                // One bad handler must not starve the others.
            }
        }
    }

    private fun markKicked() {
        // Caller holds the lock.
        // KICK then reconnect would just fight the new device, so auto
        // reconnect stays off and the user is sent back to login. The state
        // flip happens HERE, not via the close event.
        kicked = true
        clearHeartbeat()
        rejectAllPending(RequestError.Kind.KICKED)
        cancelReconnectTimer()
        setStatus(ConnState.KICKED)
        ws?.close()
        ws = null
    }

    private fun handleTransportClosed(t: WsTransport) {
        synchronized(lock) {
            if (ws !== t) return  // stale transport (superseded attempt or manual path)
            ws = null
            clearHeartbeat()
            rejectAllPending(RequestError.Kind.CLOSED)
            decoder.reset()
            if (kicked) {
                setStatus(ConnState.KICKED)
                return
            }
            if (state == ConnState.CLOSED) return  // manual disconnect()
            setStatus(ConnState.WAITING_RECONNECT)
            scheduleReconnect()
        }
    }

    private fun scheduleReconnect() {
        // Caller holds the lock. base = min(base·2^attempt, max) — iterated
        // doubling with an in-loop clamp reaches the same value without
        // overflow for any reachable attempt count.
        var base = options.reconnectBaseMs
        repeat(attempt.coerceAtMost(60)) {
            if (base < options.reconnectMaxMs) base = (base shl 1).coerceAtMost(options.reconnectMaxMs)
        }
        val jitter = (base * options.jitterRatio).roundToLong()
        val delay = base + random.nextLong(jitter * 2 + 1) - jitter
        attempt++
        reconnectListeners.forEach { listener ->
            try {
                listener(attempt, delay)
            } catch (_: Exception) {
                // Listener errors must not break the reconnect chain.
            }
        }
        reconnectCancellable = scheduler.post(delay) {
            synchronized(lock) { reconnectCancellable = null }
            // A failed attempt loops straight back through handleTransportClosed.
            runCatching { connect() }
        }
    }

    private fun cancelReconnectTimer() {
        // Caller holds the lock.
        reconnectCancellable?.cancel()
        reconnectCancellable = null
    }

    private fun rejectAllPending(kind: RequestError.Kind) {
        // Caller holds the lock.
        for (entry in pending.values) {
            entry.timeout.cancel()
            entry.raw.completeExceptionally(RequestError(kind))
        }
        pending.clear()
    }

    companion object {
        /**
         * Process-wide daemon ticker shared by all connections (ticks are
         * cheap state guards after disconnect; one thread per VM, never shut
         * down). Tests inject a manual scheduler instead.
         */
        private val SHARED_SCHEDULER: Scheduler = ExecutorScheduler()
    }
}
