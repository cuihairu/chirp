package chirp.mobile

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build

/**
 * 通知呈现面（FCM 接收侧）：消息类推送落到一个专属 channel，点击跳
 * [MainActivity] 并把会话键带进去（[PushRouteParser] 路由结果）。
 *
 * 无 androidx（对齐本壳「纯框架 Views」约定），只用平台 API。图标用平台
 * 内置 drawable——本壳没有自己的 res/drawable（连启动图标都走系统默认）。
 */
object ChirpNotifications {

    const val CHANNEL_ID = "chirp_messages"

    /** 深链携带的会话键 extra（"p:a|b" / "g:gid"）。 */
    const val EXTRA_ROUTE_KEY = "chirp_route_key"

    /** 通知权限申请回执句柄。 */
    const val REQUEST_NOTIFICATIONS = 1

    /** POST_NOTIFICATIONS 是 API 33 起的运行时权限；更老的版本隐式授予。 */
    fun hasPermission(context: Context): Boolean =
        Build.VERSION.SDK_INT < 33 ||
            context.checkSelfPermission("android.permission.POST_NOTIFICATIONS") ==
                PackageManager.PERMISSION_GRANTED

    fun requestPermission(activity: MainActivity) {
        if (Build.VERSION.SDK_INT >= 33 &&
            activity.checkSelfPermission("android.permission.POST_NOTIFICATIONS")
                != PackageManager.PERMISSION_GRANTED
        ) {
            activity.requestPermissions(
                arrayOf("android.permission.POST_NOTIFICATIONS"),
                REQUEST_NOTIFICATIONS,
            )
        }
    }

    /** 幂等建 channel；channel 一旦建了就改不了属性，重复调用无害。 */
    fun ensureChannel(context: Context) {
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE)
            as NotificationManager
        if (manager.getNotificationChannel(CHANNEL_ID) != null) return
        val channel = NotificationChannel(
            CHANNEL_ID,
            "消息",
            NotificationManager.IMPORTANCE_DEFAULT,
        )
        channel.description = "会话消息与 @ 提及"
        manager.createNotificationChannel(channel)
    }

    /**
     * 呈现一条通知。同一会话键复用同一个 notification id（String.hashCode
     * 的 Java 规范算法固定，跨进程一致），新消息覆盖旧的——按会话堆叠，
     * 不按条轰炸。路由不出的推送（routeKey 为 null）仍照常呈现，只是点了
     * 只回到主界面。返回 false = 未呈现（通知权限未给）。
     */
    fun post(context: Context, title: String, body: String, routeKey: String?): Boolean {
        if (!hasPermission(context)) return false
        val content = PendingIntent.getActivity(
            context,
            0,
            Intent(context, MainActivity::class.java)
                .setAction(Intent.ACTION_VIEW)
                .putExtra(EXTRA_ROUTE_KEY, routeKey),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val builder = Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(Notification.BigTextStyle().bigText(body))
            .setAutoCancel(true)
            .setContentIntent(content)
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE)
            as NotificationManager
        manager.notify((routeKey?.hashCode() ?: 0), builder.build())
        return true
    }
}
