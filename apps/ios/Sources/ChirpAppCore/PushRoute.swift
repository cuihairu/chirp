import Foundation

/// APNs 接收面(P5):payload → 可导航路由 + 前台呈现裁决。
///
/// 服务端契约(逆读 `BuildAPNsPayload`/`PushBridge::NotifyOffline`):自定义
/// 数据平铺在顶层 `chirp_data` 键下(`type`/`from_user_id`/`channel_id`/
/// `channel_type`/`message_id`),`click_action` 深链只在 PushNotificationRequest
/// proto 请求面、**不进 APNs 载荷**——深链只能从 chirp_data 反解。纯函数,
/// 壳层(AppDelegate)只做线程跳转,判定逻辑全在这里可被 `swift test` 覆盖。
public enum PushRouteParser {
    /// 可路由的推送类别:只有会话消息值得深链。`call`(语音来电)、
    /// `badge_update`(角标同步,notification_service.cc)与未知类型一律
    /// 不路由——点了也没有对应会话面。
    public enum Kind: Equatable {
        case message
        case mention
    }

    /// 一条可路由推送:类别 + 会话键('p:a|b' / 'g:gid',与
    /// `SessionChannel.key`/web conversation key 同构)。
    public struct Route: Equatable {
        public let kind: Kind
        public let channelKey: String

        public init(kind: Kind, channelKey: String) {
            self.kind = kind
            self.channelKey = channelKey
        }
    }

    /// 解析 APNs `userInfo`。路由不出的任何情形(call/badge_update/未知类型/
    /// 无 chirp_data/无频道/team·world 频道/畸形键)一律返回 nil——由调用方
    /// 按「照常呈现、不导航」处理,宁可不跳不猜。
    public static func route(from userInfo: [AnyHashable: Any]) -> Route? {
        guard let data = userInfo["chirp_data"] as? [String: Any],
            let type = data["type"] as? String
        else { return nil }

        let kind: Kind
        switch type {
        case "message": kind = .message
        case "mention": kind = .mention
        default: return nil  // call / badge_update / silent 杂项 / 未知
        }

        guard let channelId = data["channel_id"] as? String, !channelId.isEmpty
        else { return nil }

        let channelType = data["channel_type"] as? String ?? ""
        let key: String
        switch channelType {
        case "guild":
            key = "g:" + channelId
        case "private":
            key = "p:" + channelId
        case "team", "world":
            // 游戏平面频道,伴侣面没有对应会话行,不猜路由。
            return nil
        case "":
            // 通知服务自有 emitter(NotifyNewMessage/NotifyMention)不带
            // channel_type:DM 键恒含 '|',群 id 形如 group_<ms>_<n> 恒不含
            // ——含 '|' 才推 DM,拿不准的保守不路由。
            guard channelId.contains("|") else { return nil }
            key = "p:" + channelId
        default:
            return nil  // 未来新增频道类型:先不路由,等有会话面再放开
        }

        // 键形态过 SessionChannel 结构门(畸形对 'a|b|c'、空段 '|' 拒收),
        // 坏键进导航栈只会开出空聊天面,不如不跳。
        guard SessionChannel(key: key, selfId: "") != nil else { return nil }
        return Route(kind: kind, channelKey: key)
    }
}

/// 前台呈现裁决(web ChatPage 同语义):**正在看该会话**才抑制横幅,
/// 其余照常呈现——不在会话里、正在看别的会话、路由不出(nil)都呈现,
/// 宁多勿漏。后台到达不走这里(iOS 默认呈现)。
public enum PushForegroundPolicy {
    public static func shouldPresent(
        activeChannelKey: String?, routeChannelKey: String?
    ) -> Bool {
        guard let active = activeChannelKey, let route = routeChannelKey
        else { return true }
        return active != route
    }
}
