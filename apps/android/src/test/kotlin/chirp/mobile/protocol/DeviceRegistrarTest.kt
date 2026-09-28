package chirp.mobile.protocol

import chirp.app_notification.AppNotification
import chirp.common.Common
import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

/** device_api.dart registerSelf 的对拍组（含空 token 降级路径）。 */
class DeviceRegistrarTest {
    private val scheduler = ManualScheduler(startMs = 1_000)
    private val transports = ArrayList<FakeWsTransport>()

    private fun connectedRegistrar(
        appVersion: String = "0.1.0",
        osVersion: String = "15",
        deviceName: String = "Google Pixel 8",
    ): Pair<DeviceRegistrar, FakeWsTransport> {
        val connection = ChatConnection(
            url = "ws://test/chirp",
            transportFactory = { FakeWsTransport().also { transports.add(it) } },
            scheduler = scheduler,
        )
        connection.connect().get(5, TimeUnit.SECONDS)
        val registrar = DeviceRegistrar(
            conn = connection,
            deviceId = { "device-1" },
            appVersion = appVersion,
            osVersion = { osVersion },
            deviceName = { deviceName },
        )
        return registrar to transports.last()
    }

    private fun wsMessage(
        msgId: Gateway.MsgID,
        sequence: Long,
        body: com.google.protobuf.MessageLite,
    ): ByteArray {
        val packet = Gateway.Packet.newBuilder()
            .setMsgId(msgId)
            .setSequence(sequence)
            .setBody(ByteString.copyFrom(body.toByteArray()))
            .build()
        return encodeFrame(packet.toByteArray())
    }

    private fun registerResponse(
        sequence: Long,
        code: Common.ErrorCode,
    ): ByteArray = wsMessage(
        Gateway.MsgID.REGISTER_DEVICE_RESP, sequence,
        AppNotification.RegisterDeviceResponse.newBuilder().setCode(code).build(),
    )

    private fun <T> unwrapped(future: java.util.concurrent.CompletableFuture<T>): T =
        try {
            future.get(5, TimeUnit.SECONDS)
        } catch (e: ExecutionException) {
            throw e.cause!!
        }

    private fun sentRequest(transport: FakeWsTransport): AppNotification.RegisterDeviceRequest {
        val packet = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.REGISTER_DEVICE_REQ, packet.msgId)
        return AppNotification.RegisterDeviceRequest.parseFrom(packet.body)
    }

    // ---- token sourcing -----------------------------------------------------

    @Test
    fun registerCarriesAllFieldsAndPropagatesOk() {
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register("user-1", PushTokenSource { it("fcm-token-1") })
        val request = sentRequest(transport)
        assertEquals("user-1", request.userId)
        assertEquals("device-1", request.deviceId)
        assertEquals("android", request.platform)
        assertEquals("fcm-token-1", request.fcmToken)
        assertEquals("0.1.0", request.appVersion)
        assertEquals("15", request.osVersion)
        assertEquals("Google Pixel 8", request.deviceName)
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.OK))
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
    }

    @Test
    fun nullTokenSourceStillRegistersWithEmptyToken() {
        // dart 降级对齐：registerSelf 在无 FCM token 时照样注册（空
        // fcm_token），设备仍清单化，推送退化为服务端日志投递。
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register("user-1", pushToken = null)
        val request = sentRequest(transport)
        assertEquals("", request.fcmToken)
        assertEquals("device-1", request.deviceId)
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.OK))
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
    }

    @Test
    fun failingLookupDegradesToEmptyTokenButStillRegisters() {
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register("user-1", PushTokenSource { it(null) })
        assertEquals("", sentRequest(transport).fcmToken)
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.OK))
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
    }

    @Test
    fun throwingSourceIsContainedAndRegistersWithEmptyToken() {
        // 源实现约定"只报告一次、不抛"，但防御性兜底：抛了的源不能让
        // registration future 永不完成，也不能发出两帧。
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register(
            "user-1",
            PushTokenSource { throw IllegalStateException("play services exploded") },
        )
        assertEquals("", sentRequest(transport).fcmToken)
        assertEquals(1, transport.sent.size)
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.OK))
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
    }

    @Test
    fun doubleReportingSourceSendsExactlyOneFrame() {
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register(
            "user-1",
            PushTokenSource { onResult ->
                onResult("first-token")
                onResult("second-token") // buggy source: reports twice
            },
        )
        assertEquals("first-token", sentRequest(transport).fcmToken)
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.OK))
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
        assertEquals(1, transport.sent.size)
    }

    // ---- response surface -----------------------------------------------------

    @Test
    fun serverErrorCodePropagates() {
        val (registrar, transport) = connectedRegistrar()
        val future = registrar.register("user-1")
        transport.deliverWsMessage(registerResponse(1, Common.ErrorCode.TARGET_OFFLINE))
        assertEquals(Common.ErrorCode.TARGET_OFFLINE, unwrapped(future))
    }

    @Test
    fun closedConnectionFailsWithRequestError() {
        val connection = ChatConnection(
            url = "ws://test",
            transportFactory = { FakeWsTransport() },
            scheduler = scheduler,
        )
        val registrar = DeviceRegistrar(connection, { "device-1" })
        val err = try {
            registrar.register("user-1").get(5, TimeUnit.SECONDS)
            kotlin.test.fail("expected failure")
        } catch (e: ExecutionException) {
            e.cause!!
        }
        assertIs<RequestError>(err)
        assertEquals(RequestError.Kind.CLOSED, err.kind)
        assertTrue(err.message!!.isNotEmpty())
    }
}
