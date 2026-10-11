package chirp.mobile.protocol

/**
 * FCM 接收面（P5，iOS `PushRouteParser` 同款语义）：data → 可导航路由 +
 * 前台呈现裁决。
 *
 * 服务端契约（逆读 `notification_service.cc`）：自定义数据平铺在 FCM 的
 * `data` 对象里（Firebase 侧即 `remoteMessage.data` 这个扁平 map，与 APNs
 * 嵌套 `chirp_data` 键不同）；`click_action` 深链只在 PushNotificationRequest
 * proto 请求面、**不进推送载荷**——深链只能从 data 反解。服务端全部
 * emitter（NotifyNewMessage/NotifyMention/call/badge_update）都不带
 * `channel_type`。纯 Kotlin 零 android 依赖，壳层与
 * FirebaseMessagingService 只做线程跳转与渲染，判定逻辑全在这里可被
 * JVM 门禁覆盖。
 */
object PushRouteParser {

    /**
     * 可路由的推送类别：只有会话消息值得深链。`call`（语音来电）、
     * `badge_update`（角标同步，notification_service.cc）与未知类型一律
     * 不路由——点了也没有对应会话面。
     */
    enum class Kind { MESSAGE, MENTION }

    /** 一条可路由推送：类别 + 会话键（"p:a|b" / "g:gid"，与 ChatPipeline
     *  的 DM 排序对键、web conversation key 同构）。 */
    data class Route(val kind: Kind, val channelKey: String)

    /**
     * 解析 FCM data。路由不出的任何情形（call/badge_update/未知类型/
     * 无频道/team·world 频道/畸形键）一律返回 null——由调用方按「照常
     * 呈现、不导航」处理，宁可不跳不猜。
     */
    fun route(data: Map<String, String>): Route? {
        val type = data["type"] ?: return null
        val kind = when (type) {
            "message" -> Kind.MESSAGE
            "mention" -> Kind.MENTION
            else -> return null // call / badge_update / silent 杂项 / 未知
        }

        val channelId = data["channel_id"]
        if (channelId.isNullOrEmpty()) return null

        val key = when (data["channel_type"] ?: "") {
            "guild" -> "g:$channelId"
            "private" -> "p:$channelId"
            "team", "world" ->
                // 游戏平面频道，伴侣面没有对应会话行，不猜路由。
                return null
            "" ->
                // 通知服务自有 emitter 不带 channel_type：DM 键恒含 '|'，
                // 群 id 形如 group_<ms>_<n> 恒不含——含 '|' 才推 DM，
                // 拿不准的保守不路由。
                if (!channelId.contains("|")) return null else "p:$channelId"
            else -> return null // 未来新增频道类型：先不路由，等有会话面再放开
        }

        // 键形态结构门（iOS SessionChannel 结构门同款）：DM 排序对必须
        // 恰两段且两段非空——畸形对 "a|b|c"、空段 "a|"/"|b" 拒收，坏键
        // 进导航栈只会开出空聊天面，不如不跳。
        if (key.startsWith("p:")) {
            val pair = key.removePrefix("p:").split('|')
            if (pair.size != 2 || pair[0].isEmpty() || pair[1].isEmpty()) return null
        }
        return Route(kind, key)
    }
}

/**
 * 前台呈现裁决（iOS `PushForegroundPolicy` / web ChatPage 同语义）：**正在
 * 看该会话**才抑制横幅，其余照常呈现——不在会话里、正在看别的会话、路由
 * 不出（null）都呈现，宁多勿漏。Android 上「正在看」= Activity 前台且打开
 * 着该会话（壳层 onResume/onPause 维护当前会话键）；后台到达由系统照常
 * 呈现，不走这里。
 */
object PushForegroundPolicy {

    fun shouldPresent(activeChannelKey: String?, routeChannelKey: String?): Boolean {
        val active = activeChannelKey ?: return true
        val route = routeChannelKey ?: return true
        return active != route
    }
}
