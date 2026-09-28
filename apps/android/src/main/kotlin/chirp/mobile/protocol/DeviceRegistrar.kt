package chirp.mobile.protocol

import chirp.app_notification.AppNotification
import chirp.common.Common
import java.util.concurrent.CompletableFuture
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Async push-token seam (app layer supplies the implementation — the
 * Firebase-backed source lives behind the `chirpPush` build switch, the
 * default build wires a no-op). Implementations MUST report a result
 * exactly once and never throw: any failure is a `null` token, and this
 * registrar degrades to dart device_api's path — register with an empty
 * `fcm_token` so the device still lists, pushes degrade to the server's
 * logging transport.
 */
fun interface PushTokenSource {
    fun fetchToken(onResult: (String?) -> Unit)
}

/**
 * Registers this install as a push target (device plane, app_gateway WS
 * 5201) — port of dart `device_api.dart#registerSelf`, minus the local
 * device-store bookkeeping the shell doesn't have yet. The server pins
 * `user_id` on the session, so the wire field is informational.
 *
 * The FCM token arrives asynchronously (Firebase tokens need Play services
 * round-trips), so [register] fetches it first and sends one
 * RegisterDeviceRequest; the future completes with the server's
 * [Common.ErrorCode] or fails with the connection-layer [RequestError].
 */
class DeviceRegistrar(
    private val conn: ChatConnection,
    private val deviceId: () -> String,
    private val appVersion: String = "",
    private val osVersion: () -> String = { "" },
    private val deviceName: () -> String = { "" },
    private val platform: String = "android",
) {
    fun register(userId: String, pushToken: PushTokenSource? = null): CompletableFuture<Common.ErrorCode> {
        val result = CompletableFuture<Common.ErrorCode>()
        val send: (String?) -> Unit = { fetched ->
            conn.request(
                MsgSpecs.registerDevice,
                AppNotification.RegisterDeviceRequest.newBuilder()
                    .setUserId(userId)
                    .setDeviceId(deviceId())
                    .setPlatform(platform)
                    // dart 降级对齐：无 token 也注册（fcm_token 留空，服务端
                    // 推送降级为日志投递），设备清单仍登记。
                    .setFcmToken(fetched ?: "")
                    .setAppVersion(appVersion)
                    .setOsVersion(osVersion())
                    .setDeviceName(deviceName())
                    .build(),
            ).whenComplete { resp, err ->
                if (err != null) result.completeExceptionally(err) else result.complete(resp.code)
            }
        }
        if (pushToken == null) {
            send(null)
            return result
        }
        // Exactly-once guard: a source that both delivered and then threw,
        // or threw after a sync delivery, must not send twice (or never).
        val delivered = AtomicBoolean(false)
        try {
            pushToken.fetchToken { token ->
                if (delivered.compareAndSet(false, true)) send(token)
            }
        } catch (t: Throwable) {
            if (delivered.compareAndSet(false, true)) send(null)
        }
        return result
    }
}
