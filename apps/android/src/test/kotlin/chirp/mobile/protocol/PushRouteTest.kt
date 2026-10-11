package chirp.mobile.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * FCM 接收面（P5）：data → 会话键路由与前台呈现裁决。向量与 iOS
 * `PushRouteTests` 同组对拍；载荷形状逆读自服务端 `NotifyNewMessage`/
 * `NotifyMention`（data 平铺、不带 channel_type、click_action 不进载荷）。
 */
class PushRouteTest {

    private fun data(
        type: String? = null,
        channelId: String? = null,
        channelType: String? = null,
    ): Map<String, String> {
        val map = mutableMapOf<String, String>()
        if (type != null) map["type"] = type
        if (channelId != null) map["channel_id"] = channelId
        if (channelType != null) map["channel_type"] = channelType
        return map
    }

    // ---- 路由：正例 -----------------------------------------------------------

    @Test
    fun messageAndMentionRouteToConversationKeys() {
        val dm = PushRouteParser.route(
            data(type = "message", channelId = "alice|bob", channelType = "private"),
        )
        assertEquals(PushRouteParser.Kind.MESSAGE, dm?.kind)
        assertEquals("p:alice|bob", dm?.channelKey, "DM 键 = 'p:' + 排序对")

        val guild = PushRouteParser.route(
            data(type = "message", channelId = "group_1700000000000_1", channelType = "guild"),
        )
        assertEquals(PushRouteParser.Kind.MESSAGE, guild?.kind)
        assertEquals("g:group_1700000000000_1", guild?.channelKey, "群键 = 'g:' + 群 id")

        // mention 与 message 同路由面，只是类别不同（渲染/未读策略无关导航）。
        val mention = PushRouteParser.route(
            data(type = "mention", channelId = "alice|bob", channelType = "private"),
        )
        assertEquals(PushRouteParser.Kind.MENTION, mention?.kind)
        assertEquals("p:alice|bob", mention?.channelKey)
    }

    @Test
    fun missingChannelTypeDerivesDmFromPairMarker() {
        // 服务端全部 emitter（NotifyNewMessage/NotifyMention）只带 type +
        // channel_id 不带 channel_type：DM 键恒含 '|'，据此反推。
        val dm = PushRouteParser.route(data(type = "message", channelId = "alice|bob"))
        assertEquals("p:alice|bob", dm?.channelKey)

        // 群 id（group_<ms>_<n>）不含 '|'：拿不准就不跳，不按前缀猜 'g:'。
        val bare = PushRouteParser.route(
            data(type = "message", channelId = "group_1700000000000_1"),
        )
        assertNull(bare, "缺 channel_type 且无对拼接标记，保守不路由")
    }

    @Test
    fun realServerEmitterShapeRoutesDm() {
        // 生产 FCM data 的真实形状（NotifyNewMessage 全部键），钉死跨端契约。
        val route = PushRouteParser.route(
            mapOf(
                "type" to "message",
                "from_user_id" to "alice",
                "channel_id" to "alice|bob",
            ),
        )
        assertEquals(PushRouteParser.Kind.MESSAGE, route?.kind)
        assertEquals("p:alice|bob", route?.channelKey)
    }

    // ---- 路由：类型面拒收 ------------------------------------------------------

    @Test
    fun nonConversationTypesNotRouted() {
        // call/badge_update 即便带足频道字段也不路由（点了没有会话面）；
        // badge_update 是通知服务的角标同步（type+badge 键），更不能当消息跳。
        val call = PushRouteParser.route(
            data(type = "call", channelId = "alice|bob", channelType = "private"),
        )
        assertNull(call, "来电推送不导航")

        val badge = PushRouteParser.route(
            mapOf(
                "type" to "badge_update",
                "badge" to "3",
                "channel_id" to "alice|bob",
                "channel_type" to "private",
            ),
        )
        assertNull(badge, "角标同步不导航")

        assertNull(
            PushRouteParser.route(
                data(type = "sync", channelId = "alice|bob", channelType = "private"),
            ),
            "未知类型不导航",
        )
        assertNull(
            PushRouteParser.route(data(channelId = "alice|bob", channelType = "private")),
            "缺 type 不导航",
        )
    }

    @Test
    fun unsupportedChannelTypesNotRouted() {
        // 游戏平面频道（team/world）在伴侣面没有会话行，跳过去只会开空面。
        assertNull(
            PushRouteParser.route(data(type = "message", channelId = "t1", channelType = "team")),
        )
        assertNull(
            PushRouteParser.route(data(type = "message", channelId = "w1", channelType = "world")),
        )
        assertNull(
            PushRouteParser.route(
                data(type = "message", channelId = "x1", channelType = "future_kind"),
            ),
            "未来新增频道类型先不放开",
        )
    }

    @Test
    fun missingDataOrChannelIdNotRouted() {
        assertNull(PushRouteParser.route(emptyMap()), "空 data")
        assertNull(
            PushRouteParser.route(data(type = "message", channelType = "private")),
            "缺 channel_id",
        )
        assertNull(
            PushRouteParser.route(data(type = "message", channelId = "", channelType = "private")),
            "空 channel_id",
        )
    }

    @Test
    fun malformedChannelIdNotRouted() {
        // 键形态结构门：三段对与空段对进导航栈只会开空聊天面。
        assertNull(
            PushRouteParser.route(
                data(type = "message", channelId = "a|b|c", channelType = "private"),
            ),
            "三段不是对",
        )
        assertNull(
            PushRouteParser.route(
                data(type = "message", channelId = "|", channelType = "private"),
            ),
            "两侧全空",
        )
        assertNull(
            PushRouteParser.route(
                data(type = "message", channelId = "a|", channelType = "private"),
            ),
            "一侧空段",
        )
    }

    // ---- 前台呈现裁决 ----------------------------------------------------------

    @Test
    fun policySuppressesOnlyWhileViewingThatConversation() {
        // 正在看这条消息所属会话 → 抑制横幅（web ChatPage 同款）。
        assertTrue(
            !PushForegroundPolicy.shouldPresent("p:alice|bob", "p:alice|bob"),
            "正在看该会话抑制",
        )
        // 正在看别的会话 → 照常呈现。
        assertTrue(PushForegroundPolicy.shouldPresent("p:alice|carol", "p:alice|bob"))
        // 不在会话里（会话列表/登录面）→ 照常呈现。
        assertTrue(PushForegroundPolicy.shouldPresent(null, "p:alice|bob"))
        // 路由不出（call/badge_update 等）→ 照常呈现，只不导航。
        assertTrue(PushForegroundPolicy.shouldPresent("p:alice|bob", null))
        assertTrue(PushForegroundPolicy.shouldPresent(null, null))
    }
}
