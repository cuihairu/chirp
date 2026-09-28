package chirp.mobile.protocol

import chirp.gateway.Gateway
import com.google.protobuf.MessageLite
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicLong

/**
 * Connection lifecycle (mirrors mobile_companion chirp_client.dart):
 *   idle → connecting → connected            (auto reconnect: later batch)
 *                    ↘ kicked                (KICK_NOTIFY: terminal)
 *   any  → closed                            (manual close(); terminal until connect())
 */
enum class ConnState { IDLE, CONNECTING, CONNECTED, KICKED, CLOSED }

/**
 * The byte-transport seam. The gateways speak WebSocket binary messages and
 * each message carries one u32-framed Packet (see [Frame]); M1 ships the
 * protocol core against this interface — the real JDK/OkHttp WebSocket
 * adapter lands with the app-shell batch, tests drive a fake transport.
 */
interface Transport {
    /** Begin delivering inbound frame payloads to [onFrame]. */
    fun start(onFrame: (ByteArray) -> Unit)

    /** Send one full frame (length prefix included). */
    fun sendFrame(frame: ByteArray)

    fun close()
}

/**
 * Minimal request/response core: sequence correlation, typed [MessageSpec]
 * round-trips, heartbeat pings, notify dispatch, KICK_NOTIFY terminal state.
 * Port of the chirp_client.dart pipeline minus auto-reconnect (staged).
 */
class ChatConnection(
    private val transportFactory: () -> Transport,
    /** 0 disables the automatic heartbeat; tests drive [sendHeartbeat] by hand. */
    val heartbeatIntervalMs: Long = 25_000,
) {
    @Volatile
    var state: ConnState = ConnState.IDLE
        private set

    private val seqCounter = AtomicLong(0)
    private val pending = ConcurrentHashMap<Long, CompletableFuture<Gateway.Packet>>()
    private val notifyListeners =
        ConcurrentHashMap<Gateway.MsgID, CopyOnWriteArrayList<(Gateway.Packet) -> Unit>>()
    private val stateListeners = CopyOnWriteArrayList<(ConnState) -> Unit>()

    @Volatile
    private var transport: Transport? = null

    /**
     * Open the transport and start the heartbeat (unless disabled). Terminal
     * states need an explicit reconnect path: KICKED never reconnects, CLOSED
     * may [connect] again.
     */
    fun connect() {
        check(state == ConnState.IDLE || state == ConnState.CLOSED) {
            "cannot connect from state $state"
        }
        state = ConnState.CONNECTING
        val t = transportFactory()
        transport = t
        t.start(::onFrame)
        state = ConnState.CONNECTED
        stateListeners.forEach { it(state) }
    }

    /**
     * Send `Packet{spec.reqMsgId, sequence, body}` and complete with the
     * decoded typed response when the server answers with the echoed
     * sequence. The response's `code` field is the caller's business.
     */
    fun <T : MessageLite> request(spec: MessageSpec<T>, body: MessageLite): CompletableFuture<T> {
        val t = transport
        if (t == null || state != ConnState.CONNECTED) {
            return CompletableFuture.failedFuture(IllegalStateException("not connected (state=$state)"))
        }
        val seq = seqCounter.incrementAndGet()
        val packet = Gateway.Packet.newBuilder()
            .setMsgId(spec.reqMsgId)
            .setSequence(seq)
            .setBody(body.toByteString())
            .build()
        val typed = CompletableFuture<T>()
        val raw = CompletableFuture<Gateway.Packet>()
        pending[seq] = raw
        raw.whenComplete { resp, err ->
            pending.remove(seq)
            if (err != null) {
                typed.completeExceptionally(err)
            } else {
                try {
                    typed.complete(spec.decodeResponse(resp.body.toByteArray()))
                } catch (e: Exception) {
                    typed.completeExceptionally(e)
                }
            }
        }
        try {
            t.sendFrame(encodeFrame(packet.toByteArray()))
        } catch (e: Exception) {
            raw.completeExceptionally(e)
        }
        return typed
    }

    /** Register a handler for one notify msgId (server-push, sequence 0). */
    fun onNotify(msgId: Gateway.MsgID, handler: (Gateway.Packet) -> Unit) {
        notifyListeners.computeIfAbsent(msgId) { CopyOnWriteArrayList() }.add(handler)
    }

    /** State transitions surface here (CONNECTED, KICKED, CLOSED). */
    fun onStateChange(handler: (ConnState) -> Unit) {
        stateListeners.add(handler)
    }

    /** One heartbeat ping; automatic when [heartbeatIntervalMs] > 0. */
    fun sendHeartbeat() {
        val t = transport ?: return
        val ping = Gateway.HeartbeatPing.newBuilder().build()
        val packet = Gateway.Packet.newBuilder()
            .setMsgId(Gateway.MsgID.HEARTBEAT_PING)
            .setSequence(0)
            .setBody(ping.toByteString())
            .build()
        t.sendFrame(encodeFrame(packet.toByteArray()))
    }

    /** Manual close; terminal until [connect]. */
    fun close() {
        stopTransport()
        state = ConnState.CLOSED
        stateListeners.forEach { it(state) }
    }

    private fun stopTransport() {
        transport?.close()
        transport = null
        failAllPending(IllegalStateException("connection closed"))
    }

    private fun failAllPending(err: Exception) {
        for ((seq, raw) in pending) {
            pending.remove(seq)
            raw.completeExceptionally(err)
        }
    }

    private fun onFrame(frame: ByteArray) {
        val packet = try {
            Gateway.Packet.parseFrom(frame)
        } catch (e: Exception) {
            // A corrupted stream cannot be resynchronized (same semantics as
            // FrameError on the raw-framing layer): the link is dead.
            stopTransport()
            state = ConnState.CLOSED
            stateListeners.forEach { it(state) }
            return
        }
        val raw = pending.remove(packet.sequence)
        if (raw != null && packet.sequence != 0L) {
            raw.complete(packet)
            return
        }
        if (packet.msgId == Gateway.MsgID.KICK_NOTIFY) {
            stopTransport()
            state = ConnState.KICKED
            stateListeners.forEach { it(state) }
        }
        notifyListeners[packet.msgId]?.forEach { it(packet) }
    }
}
