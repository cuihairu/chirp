package chirp.mobile.push

import android.content.Context
import chirp.mobile.protocol.PushTokenSource

/**
 * The default build (no `chirpPush` flag): no Firebase on the classpath,
 * every lookup reports `null`. DeviceRegistrar degrades to dart
 * device_api's path — registers with an empty `fcm_token`, so the device
 * still lists and pushes degrade to the server's logging transport.
 */
class PlatformPushTokenSource(@Suppress("UNUSED_PARAMETER") private val context: Context) : PushTokenSource {
    override fun fetchToken(onResult: (String?) -> Unit) {
        onResult(null)
    }
}
