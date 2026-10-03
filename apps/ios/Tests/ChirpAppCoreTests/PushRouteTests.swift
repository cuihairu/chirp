import XCTest
@testable import ChirpAppCore

/// APNs 接收面(P5):payload → 会话键路由与前台呈现裁决。载荷形状逆读自
/// 服务端 `BuildAPNsPayload`(chirp_data 平铺、click_action 不进载荷)。
final class PushRouteTests: XCTestCase {
    /// 拼一个 APNs userInfo:aps 壳 + chirp_data 键表(缺省字段即「不带」)。
    private func payload(
        type: String? = nil,
        channelId: String? = nil,
        channelType: String? = nil,
        extra: [String: String] = [:]
    ) -> [AnyHashable: Any] {
        var data = extra
        if let type { data["type"] = type }
        if let channelId { data["channel_id"] = channelId }
        if let channelType { data["channel_type"] = channelType }
        return [
            "aps": ["alert": ["title": "t", "body": "b"], "content-available": 1],
            "chirp_data": data,
        ]
    }

    // ---- 路由:正例 ------------------------------------------------------------

    func testMessageAndMentionRouteToConversationKeys() {
        let dm = PushRouteParser.route(
            from: payload(type: "message", channelId: "alice|bob", channelType: "private"))
        XCTAssertEqual(dm?.kind, .message)
        XCTAssertEqual(dm?.channelKey, "p:alice|bob", "DM 键 = 'p:' + 排序对")

        let guild = PushRouteParser.route(
            from: payload(type: "message", channelId: "group_1700000000000_1", channelType: "guild"))
        XCTAssertEqual(guild?.kind, .message)
        XCTAssertEqual(guild?.channelKey, "g:group_1700000000000_1", "群键 = 'g:' + 群 id")

        // mention 与 message 同路由面,只是类别不同(渲染/未读策略无关导航)。
        let mention = PushRouteParser.route(
            from: payload(type: "mention", channelId: "alice|bob", channelType: "private"))
        XCTAssertEqual(mention?.kind, .mention)
        XCTAssertEqual(mention?.channelKey, "p:alice|bob")
    }

    func testMissingChannelTypeDerivesDmFromPairMarker() {
        // 通知服务自有 emitter(NotifyNewMessage/NotifyMention)只带 type +
        // channel_id 不带 channel_type:DM 键恒含 '|',据此反推。
        let dm = PushRouteParser.route(from: payload(type: "message", channelId: "alice|bob"))
        XCTAssertEqual(dm?.channelKey, "p:alice|bob")

        // 群 id(group_<ms>_<n>)不含 '|':拿不准就不跳,不按前缀猜 'g:'。
        let bare = PushRouteParser.route(
            from: payload(type: "message", channelId: "group_1700000000000_1"))
        XCTAssertNil(bare, "缺 channel_type 且无对拼接标记,保守不路由")
    }

    // ---- 路由:类型面拒收 ------------------------------------------------------

    func testNonConversationTypesNotRouted() {
        // call/badge_update 即便带足频道字段也不路由(点了没有会话面);
        // badge_update 是通知服务的角标同步(type+badge 键),更不能当消息跳。
        let call = PushRouteParser.route(
            from: payload(type: "call", channelId: "alice|bob", channelType: "private"))
        XCTAssertNil(call, "来电推送不导航")

        let badge = PushRouteParser.route(
            from: payload(type: "badge_update", channelId: "alice|bob",
                channelType: "private", extra: ["badge": "3"]))
        XCTAssertNil(badge, "角标同步不导航")

        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "sync", channelId: "alice|bob", channelType: "private")),
            "未知类型不导航")
        XCTAssertNil(PushRouteParser.route(
            from: payload(channelId: "alice|bob", channelType: "private")),
            "缺 type 不导航")
    }

    func testUnsupportedChannelTypesNotRouted() {
        // 游戏平面频道(team/world)在伴侣面没有会话行,跳过去只会开空面。
        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "message", channelId: "t1", channelType: "team")))
        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "message", channelId: "w1", channelType: "world")))
        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "message", channelId: "x1", channelType: "future_kind")),
            "未来新增频道类型先不放开")
    }

    func testMissingChirpDataOrChannelIdNotRouted() {
        XCTAssertNil(PushRouteParser.route(from: [:]), "空 userInfo")
        XCTAssertNil(PushRouteParser.route(from: ["aps": ["badge": 1]]), "无 chirp_data")
        XCTAssertNil(
            PushRouteParser.route(from: ["chirp_data": [String: String]()]),
            "空 chirp_data(无 type)")
        XCTAssertNil(
            PushRouteParser.route(from: payload(type: "message", channelType: "private")),
            "缺 channel_id")
        XCTAssertNil(
            PushRouteParser.route(from: payload(type: "message", channelId: "", channelType: "private")),
            "空 channel_id")
    }

    func testMalformedChannelIdNotRouted() {
        // 键形态过 SessionChannel 结构门:三段对与空段对进导航栈只会开空聊天面。
        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "message", channelId: "a|b|c", channelType: "private")),
            "三段不是对")
        XCTAssertNil(PushRouteParser.route(
            from: payload(type: "message", channelId: "|", channelType: "private")),
            "两侧全空")
    }

    func testRouteFromNestedApnsPayloadShape() {
        // 真实 userInfo 是嵌套字典(aps 壳 + chirp_data 平面),钉死跨层取值。
        let userInfo: [AnyHashable: Any] = [
            "aps": [
                "alert": ["title": "alice", "body": "hi"],
                "badge": 3,
                "content-available": 1,
            ],
            "chirp_data": [
                "type": "message",
                "from_user_id": "alice",
                "channel_id": "alice|bob",
                "channel_type": "private",
                "message_id": "m1",
            ],
        ]
        let route = PushRouteParser.route(from: userInfo)
        XCTAssertEqual(route?.kind, .message)
        XCTAssertEqual(route?.channelKey, "p:alice|bob")
    }

    // ---- 前台呈现裁决 ----------------------------------------------------------

    func testPolicySuppressesOnlyWhileViewingThatConversation() {
        // 正在看这条消息所属会话 → 抑制横幅(web ChatPage 同款)。
        XCTAssertFalse(PushForegroundPolicy.shouldPresent(
            activeChannelKey: "p:alice|bob", routeChannelKey: "p:alice|bob"))
        // 正在看别的会话 → 照常呈现。
        XCTAssertTrue(PushForegroundPolicy.shouldPresent(
            activeChannelKey: "p:alice|carol", routeChannelKey: "p:alice|bob"))
        // 不在会话里(会话列表/登录面)→ 照常呈现。
        XCTAssertTrue(PushForegroundPolicy.shouldPresent(
            activeChannelKey: nil, routeChannelKey: "p:alice|bob"))
        // 路由不出(call/badge_update 等)→ 照常呈现,只不导航。
        XCTAssertTrue(PushForegroundPolicy.shouldPresent(
            activeChannelKey: "p:alice|bob", routeChannelKey: nil))
        XCTAssertTrue(PushForegroundPolicy.shouldPresent(
            activeChannelKey: nil, routeChannelKey: nil))
    }
}
