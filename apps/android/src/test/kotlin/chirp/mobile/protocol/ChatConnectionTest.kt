package chirp.mobile.protocol

import chirp.auth.Auth
import chirp.common.Common
import chirp.gateway.Gateway
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ChatConnectionTest {
    private val scheduler = ManualScheduler(startMs = 1_000)
    private val transports = ArrayList<FakeWsTransport>()

    /** One-shot script: the next transport created by the factory fails open with this. */
    private var nextFailOpen: Throwable? = null

    /** Fresh connection on a scripted fake; auto-opens like a happy path. */
    private fun connection(
        options: ChirpClientOptions = ChirpClientOptions(),
        random: kotlin.random.Random = FakeRandom(),
    ): ChatConnection {
        val connection = ChatConnection(
            url = "ws://test/chirp",
            transportFactory = {
                FakeWsTransport().also {
                    it.failOpenWith = nextFailOpen
                    nextFailOpen = null
                    transports.add(it)
                }
            },
            options = options,
            random = random,
            scheduler = scheduler,
        )
        return connection
    }

    private fun connect(connection: ChatConnection): FakeWsTransport {
        connection.connect().get(5, TimeUnit.SECONDS)
        return transports.last()
    }

    /** Inbound server Packet as a full WebSocket message (framed). */
    private fun wsMessage(msgId: Gateway.MsgID, sequence: Long, body: com.google.protobuf.MessageLite): ByteArray =
        encodeFrame(
            Gateway.Packet.newBuilder()
                .setMsgId(msgId)
                .setSequence(sequence)
                .setBody(body.toByteString())
                .build()
                .toByteArray(),
        )

    @Test
    fun loginRoundTripCorrelatesBySequenceAndDecodesTheTypedResponse() {
        val connection = connection()
        val transport = connect(connection)
        assertEquals(ConnState.CONNECTED, connection.state)

        val request = Auth.LoginRequest.newBuilder()
            .setToken("t-1")
            .setDeviceId("dev-1")
            .setPlatform("android")
            .build()
        val future = connection.request(MsgSpecs.login, request)

        // The outgoing frame carries Packet{LOGIN_REQ, seq=1, body}.
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.LOGIN_REQ, sent.msgId)
        assertEquals(1L, sent.sequence)
        assertEquals("t-1", Auth.LoginRequest.parseFrom(sent.body).token)

        // Server answers with the RESP msgId and the echoed sequence.
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 1L,
                Auth.LoginResponse.newBuilder().setSessionId("s-9").setUserId("u-1").build(),
            ),
        )
        val response = future.get(5, TimeUnit.SECONDS)
        assertEquals(Common.ErrorCode.OK, response.code)
        assertEquals("s-9", response.sessionId)
    }

    @Test
    fun errorResponsesStillCompleteSoCallersInspectTheCode() {
        val connection = connection()
        val transport = connect(connection)
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 1L,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.AUTH_FAILED).build(),
            ),
        )
        assertEquals(
            Common.ErrorCode.AUTH_FAILED,
            future.get(5, TimeUnit.SECONDS).code,
        )
    }

    @Test
    fun requestBeforeConnectFailsFastWithoutTouchingTheTransport() {
        val connection = connection()
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        // get() wraps the completion exception; the cause carries the kind.
        val err = assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
        assertTrue(err.cause is RequestError)
        assertEquals(RequestError.Kind.CLOSED, (err.cause as RequestError).kind)
        assertEquals(0, transports.size)
    }

    @Test
    fun requestTimesOutWhenNoResponseArrivesAndLateResponsesAreIgnored() {
        val connection = connection()
        val transport = connect(connection)
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build(), timeoutMs = 1_000)
        scheduler.advance(1_000)
        val err = assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
        assertEquals(RequestError.Kind.TIMEOUT, (err.cause as RequestError).kind)

        // A response to an already timed-out request must not throw.
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.LOGIN_RESP, 1L, Auth.LoginResponse.newBuilder().build()),
        )
    }

    @Test
    fun heartbeatPingConsumesSequenceAndCarriesTheVirtualClockTimestamp() {
        val connection = connection()
        val transport = connect(connection)
        scheduler.advance(25_000)
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.HEARTBEAT_PING, sent.msgId)
        assertEquals(1L, sent.sequence)
        // The tick fires at virtual 26000 (start 1000 + interval).
        assertEquals(26_000L, Gateway.HeartbeatPing.parseFrom(sent.body).timestamp)
    }

    @Test
    fun pongResetsMissedPongsAndRecordsClockOffset() {
        val connection = connection()
        val transport = connect(connection)
        // Two ping ticks without any pong exhaust maxMissedPongs=2; the third
        // would kill the link. A pong between ticks resets the counter.
        scheduler.advance(25_000)
        scheduler.advance(25_000)
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.HEARTBEAT_PONG, 2L,
                Gateway.HeartbeatPong.newBuilder().setServerTime(1_400).build(),
            ),
        )
        // Two ticks moved the virtual clock 1000 → 51000.
        assertEquals(1_400L - 51_000L, connection.clockOffsetMs)
        // Survives the third tick that would have closed a pong-less link.
        scheduler.advance(25_000)
        assertEquals(0, transport.closeCount)
        assertTrue(transport.sent.size >= 3)
    }

    @Test
    fun missedPongsBeyondTheLimitCloseTheLinkIntoReconnect() {
        val connection = connection()
        val transport = connect(connection)
        scheduler.advance(25_000 * 3) // maxMissedPongs=2: the 3rd tick kills the link
        assertEquals(1, transport.closeCount)
        assertEquals(ConnState.WAITING_RECONNECT, connection.state)
        assertTrue(scheduler.pendingTasks() > 0) // backoff timer armed
    }

    @Test
    fun abnormalCloseSchedulesBackoffReconnectAndSuccessFiresReconnected() {
        // FakeRandom returns the jitter midpoint, so the scheduled delay
        // equals base exactly and the ladder is assertable.
        val connection = connection(random = FakeRandom())
        val reconnecting = ArrayList<Pair<Int, Long>>()
        var reconnected = 0
        connection.onReconnecting { attempt, delay -> reconnecting.add(attempt to delay) }
        connection.onReconnected { reconnected += 1 }

        val transport = connect(connection)
        transport.closedHandler!!.invoke() // remote drop without our close()
        assertEquals(ConnState.WAITING_RECONNECT, connection.state)
        // FakeRandom(jitter=0): delay == base exactly, attempt 1-based.
        assertEquals(listOf(1 to 500L), reconnecting)
        assertEquals(0, reconnected)

        scheduler.advance(500)
        assertEquals(2, transports.size) // the timer reconnected
        assertEquals(ConnState.CONNECTED, connection.state)
        assertEquals(1, reconnected)

        // The counter keeps climbing across drops until resetBackoff().
        transports[1].closedHandler!!.invoke()
        assertEquals(listOf(1 to 500L, 2 to 1_000L), reconnecting)
        connection.resetBackoff()
        // Fire the pending timer so the third socket exists, then drop it.
        scheduler.advance(1_000)
        assertEquals(3, transports.size)
        transports[2].closedHandler!!.invoke()
        // After the reset the next drop restarts the ladder at attempt 1.
        assertEquals(1 to 500L, reconnecting.last())
    }

    @Test
    fun failedReconnectAttemptLoopsBackIntoBackoff() {
        val connection = connection()
        connect(connection)
        transports[0].closedHandler!!.invoke()
        // Script the transport that the reconnect timer is about to create.
        nextFailOpen = java.io.IOException("server down")
        scheduler.advance(500)
        // The attempt opened, failed, and scheduled the next try (attempt 2).
        assertEquals(ConnState.WAITING_RECONNECT, connection.state)
        assertEquals(2, transports.size)
        scheduler.advance(1_000)
        assertEquals(3, transports.size) // third transport opening now
    }

    @Test
    fun kickedIsTerminalForAutoReconnectButExplicitConnectRestarts() {
        val connection = connection()
        val transport = connect(connection)
        val states = ArrayList<ConnState>()
        connection.onStateChange { states.add(it) }

        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.KICK_NOTIFY, 0L, Auth.KickNotify.newBuilder().setReason("logged in elsewhere").build()),
        )

        assertEquals(ConnState.KICKED, connection.state)
        assertTrue(connection.kicked)
        assertEquals(1, transport.closeCount)
        assertTrue(states.contains(ConnState.KICKED))
        // No reconnect timer may be armed behind a kick.
        assertEquals(0, scheduler.pendingTasks())
        transport.closedHandler!!.invoke() // the transport still goes down later
        assertEquals(ConnState.KICKED, connection.state)
        assertEquals(0, scheduler.pendingTasks())
        // In-flight requests were flushed with the kicked kind.
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        val err = assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
        assertEquals(RequestError.Kind.CLOSED, (err.cause as RequestError).kind)

        // An explicit connect() is a fresh user action: clears the kick.
        connection.connect().get(5, TimeUnit.SECONDS)
        assertEquals(ConnState.CONNECTED, connection.state)
        assertFalse(connection.kicked)
    }

    @Test
    fun disconnectCancelsThePendingReconnectTimer() {
        val connection = connection()
        val transport = connect(connection)
        transport.closedHandler!!.invoke()
        assertEquals(ConnState.WAITING_RECONNECT, connection.state)
        connection.disconnect()
        assertEquals(ConnState.CLOSED, connection.state)
        assertEquals(0, scheduler.pendingTasks())
        scheduler.advance(60_000) // the cancelled timer must not resurrect anything
        assertEquals(1, transports.size)
        assertEquals(ConnState.CLOSED, connection.state)
    }

    @Test
    fun kickPendingRequestsAreFlushedWithTheKickedKind() {
        val connection = connection()
        val transport = connect(connection)
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.KICK_NOTIFY, 0L, Auth.KickNotify.newBuilder().build()),
        )
        val err = assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
        assertEquals(RequestError.Kind.KICKED, (err.cause as RequestError).kind)
    }

    @Test
    fun notifiesDispatchToRegisteredHandlersUntilUnsubscribed() {
        val connection = connection()
        val transport = connect(connection)
        var seen = 0
        val unsubscribe = connection.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY) { seen += 1 }

        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0L, Gateway.HeartbeatPing.getDefaultInstance()),
        )
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0L, Gateway.HeartbeatPing.getDefaultInstance()),
        )
        assertEquals(2, seen)
        unsubscribe()
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0L, Gateway.HeartbeatPing.getDefaultInstance()),
        )
        assertEquals(2, seen)
        // Notify dispatch left the link untouched.
        assertEquals(0, transport.closeCount)
        assertNull(connection.clockOffsetMs)
    }

    @Test
    fun undecodablePacketsAreIgnoredButCorruptFramingKillsTheLink() {
        val connection = connection()
        val transport = connect(connection)
        // Undecodable packet payload inside a valid frame: ignored.
        transport.deliverWsMessage(encodeFrame(byteArrayOf((0x3F), (0x0A)))) // field 7 variant garbage
        assertEquals(ConnState.CONNECTED, connection.state)
        assertEquals(0, transport.closeCount)
        // Corrupt framing (prefix lies about the length → FrameError): dead
        // link, the close event schedules the reconnect.
        val evil = ByteArray(4)
        evil[0] = 0x7F
        transport.deliverWsMessage(evil)
        assertEquals(1, transport.closeCount)
        assertEquals(ConnState.WAITING_RECONNECT, connection.state)
    }

    @Test
    fun disconnectThenExplicitConnectOpensAFreshSocket() {
        val connection = connection()
        val transport = connect(connection)
        connection.disconnect()
        assertEquals(ConnState.CLOSED, connection.state)
        assertTrue(transport.closeCount >= 1)
        assertFailsWith<ExecutionException> {
            connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build()).get(5, TimeUnit.SECONDS)
        }
        // Dart semantics: closed may connect() again.
        connect(connection)
        assertEquals(2, transports.size)
        assertEquals(ConnState.CONNECTED, connection.state)
    }

    @Test
    fun sendFireAndForgetConsumesSequenceAndThrowsWhenNotConnected() {
        val connection = connection()
        assertFailsWith<RequestError> {
            connection.send(Gateway.MsgID.MESSAGE_ACK, ByteArray(0))
        }
        val transport = connect(connection)
        connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        connection.send(Gateway.MsgID.MESSAGE_ACK, byteArrayOf(9))
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.MESSAGE_ACK, sent.msgId)
        assertEquals(2L, sent.sequence) // the login request consumed 1
    }
}
