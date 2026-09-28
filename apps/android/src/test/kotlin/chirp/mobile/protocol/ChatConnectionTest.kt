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
import kotlin.test.assertTrue

class ChatConnectionTest {
    // FakeTransport.deliver takes the payload *inside* the length prefix
    // (Transport is the framing seam; the prefix itself is FrameTest's job).
    private fun packet(msgId: Gateway.MsgID, sequence: Long, body: com.google.protobuf.MessageLite): ByteArray =
        Gateway.Packet.newBuilder()
            .setMsgId(msgId)
            .setSequence(sequence)
            .setBody(body.toByteString())
            .build()
            .toByteArray()

    @Test
    fun loginRoundTripCorrelatesBySequenceAndDecodesTheTypedResponse() {
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
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
        transport.deliver(
            packet(
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
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        transport.deliver(
            packet(
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
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        // get() wraps the completion exception; the cause carries the failure.
        val err = assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
        assertTrue(err.cause is IllegalStateException)
        assertEquals(0, transport.sent.size)
    }

    @Test
    fun heartbeatPingGoesOutWithSequenceZero() {
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
        connection.sendHeartbeat()
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.HEARTBEAT_PING, sent.msgId)
        assertEquals(0L, sent.sequence)
        val ping = Gateway.HeartbeatPing.parseFrom(sent.body)
        assertEquals(0L, ping.timestamp)
    }

    @Test
    fun kickNotifyIsTerminalAndClosesTheTransport() {
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
        val states = ArrayList<ConnState>()
        connection.onStateChange { states.add(it) }

        transport.deliver(
            packet(Gateway.MsgID.KICK_NOTIFY, 0L, Auth.KickNotify.newBuilder().setReason("logged in elsewhere").build()),
        )

        assertEquals(ConnState.KICKED, connection.state)
        assertTrue(transport.closed)
        assertTrue(states.contains(ConnState.KICKED))
        // Kicked is terminal: a new request cannot resurrect the link.
        val future = connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build())
        assertFailsWith<ExecutionException> { future.get(5, TimeUnit.SECONDS) }
    }

    @Test
    fun notifiesDispatchToRegisteredHandlers() {
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
        var seen = 0
        connection.onNotify(Gateway.MsgID.CHAT_MESSAGE_NOTIFY) { seen += 1 }

        transport.deliver(packet(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0L, Gateway.HeartbeatPing.getDefaultInstance()))
        transport.deliver(packet(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0L, Gateway.HeartbeatPing.getDefaultInstance()))
        assertEquals(2, seen)
        // Requests are not notifies: a pending correlation is consumed once.
        assertFalse(transport.closed)
    }

    @Test
    fun closeIsTerminalUntilANewConnect() {
        val transport = FakeTransport()
        val connection = ChatConnection({ transport }, heartbeatIntervalMs = 0)
        connection.connect()
        connection.close()
        assertEquals(ConnState.CLOSED, connection.state)
        assertTrue(transport.closed)
        assertFailsWith<ExecutionException> {
            connection.request(MsgSpecs.login, Auth.LoginRequest.newBuilder().build()).get(5, TimeUnit.SECONDS)
        }
        // Dart semantics: closed may connect() again, kicked may not.
        connection.connect()
        assertEquals(ConnState.CONNECTED, connection.state)
    }
}
