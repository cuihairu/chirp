package chirp.mobile.push

import android.content.Context
import chirp.mobile.protocol.PushTokenSource
import com.google.firebase.FirebaseApp
import com.google.firebase.messaging.FirebaseMessaging
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

/**
 * The `chirpPush=true` build: fetch the FCM registration token from
 * FirebaseMessaging.
 *
 * 假设（本批显式注明）：仓库里的 `google-services.json` 是占位文件（无真
 * 凭据）。占位值让 FirebaseApp 初始化照常成功，但对 Firebase 后端的
 * token 请求必然失败 → `onResult(null)` → DeviceRegistrar 按 dart
 * device_api 的降级路径注册空 token。换上真凭据（同路径覆盖该文件）即
 * 通，代码零改动。Play services 缺失的设备走同一 null 降级。
 */
class PlatformPushTokenSource(@Suppress("UNUSED_PARAMETER") private val context: Context) : PushTokenSource {
    override fun fetchToken(onResult: (String?) -> Unit) {
        val done = CompletableFuture<String?>()
        try {
            // Returns null when no google-services resources were generated;
            // idempotent on later calls (the app instance wins).
            if (FirebaseApp.initializeApp(context) == null) {
                onResult(null)
                return
            }
            FirebaseMessaging.getInstance().token
                .addOnSuccessListener { done.complete(it) }
                .addOnFailureListener { done.complete(null) }
                .addOnCanceledListener { done.complete(null) }
        } catch (t: Throwable) {
            // No Play services → getInstance() throws; degrade like dart.
            done.complete(null)
        }
        // Play services can hang on the token round-trip: bound it so
        // device registration can't stall forever behind it.
        done.orTimeout(10, TimeUnit.SECONDS)
            .whenComplete { token, err -> onResult(if (err == null) token else null) }
    }
}
