package chirp.mobile.push

import android.util.Log
import chirp.mobile.ActiveChannel
import chirp.mobile.ChirpNotifications
import chirp.mobile.protocol.PushForegroundPolicy
import chirp.mobile.protocol.PushRouteParser
import chirp.mobile.protocol.PushTokenHub
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage

/**
 * FCM 接收面（P5，对齐 iOS AppDelegate 同款判定）：data 经
 * [PushRouteParser] 反解路由，「正在看该会话」由 [PushForegroundPolicy]
 * 抑制横幅（在线通知已由 CHAT_MESSAGE_NOTIFY 在壳内渲染，重复弹是噪音），
 * 其余照常呈现并可点击深链。
 *
 * 仅 `-PchirpPush=true` 构建编译本类（firebase-messaging 只在该构建上
 * classpath）；manifest 的 service 条目无条件存在，默认构建下惰性不生效。
 *
 * token 面（注册侧，M3.5 已落 + 刷新臂）：onNewToken 把刷新 token 记入
 * [PushTokenHub]（进程级 latest + 监听扇出）——登录中的壳层监听随即在既有
 * 连接上重注册（服务端 RegisterDevice 按 device_id create-or-update，同键
 * 覆盖即换 token）；未登录时只记 latest，下次登录本就注册最新 token。
 */
class ChirpFirebaseService : FirebaseMessagingService() {

    override fun onMessageReceived(remoteMessage: RemoteMessage) {
        val route = PushRouteParser.route(remoteMessage.data)
        if (route != null &&
            !PushForegroundPolicy.shouldPresent(ActiveChannel.key, route.channelKey)
        ) {
            return // 正在看该会话：抑制横幅，壳内日志已在渲染这条消息
        }
        val notification = remoteMessage.notification
        ChirpNotifications.post(
            context = this,
            title = notification?.title
                ?: remoteMessage.data["from_user_id"]
                ?: "chirp",
            body = notification?.body ?: "",
            routeKey = route?.channelKey,
        )
    }

    override fun onNewToken(token: String) {
        // 刷新臂：latest 记录 + 监听扇出（登录中的壳层即重注册）。
        PushTokenHub.remember(token)
        Log.i(TAG, "fcm token refreshed; fan-out dispatched")
    }

    private companion object {
        const val TAG = "ChirpPush"
    }
}
