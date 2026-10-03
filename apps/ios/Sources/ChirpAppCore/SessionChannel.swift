import ChirpProtos
import Foundation

/// 会话频道引用(P4e;web conversation key 同位):DM 与群共用一个键命名
/// 空间——导航值、会话行、未读与历史都按 key 寻址。键形态与 web
/// models.ts 逐字同构:'p:' + 排序对 'a|b'(私聊)、'g:' + 群 id(群)。
public struct SessionChannel: Equatable {
    public enum Kind: Equatable {
        case dm
        case group
    }

    /// 服务端频道 id:DM = 排序对 'a|b',群 = 群 id。
    public let channelId: String
    /// 对端用户 id(DM)/群 id(群)——web Conversation.peerId 同义。
    public let peerId: String
    public let kind: Kind

    /// 导航与记账用的键('p:a|b' / 'g:gid')。
    public var key: String {
        switch kind {
        case .dm: return "p:" + channelId
        case .group: return "g:" + channelId
        }
    }

    /// wire 面频道类型:群聊走 GUILD(web channelTypeOf 同款)。
    public var channelType: Chirp_Chat_ChannelType {
        switch kind {
        case .dm: return .private
        case .group: return .guild
        }
    }

    /// DM 引用:频道 id 与管线同款拼法(排序对,两侧落同一桶)。
    public static func dm(selfId: String, peerId: String) -> SessionChannel {
        SessionChannel(
            kind: .dm, channelId: SessionIndex.dmChannelId(selfId, peerId), peerId: peerId)
    }

    public static func group(_ groupId: String) -> SessionChannel {
        SessionChannel(kind: .group, channelId: groupId, peerId: groupId)
    }

    /// 从导航键反解。'p:' 的对端 = 不是自己的那一侧(两侧同 id 时取后位);
    /// 结构不对返回 nil(登出竞态/坏键,壳层据此拒绝打开)。
    public init?(key: String, selfId: String) {
        if key.hasPrefix("p:") {
            let pair = String(key.dropFirst(2)).split(separator: "|", omittingEmptySubsequences: false)
            guard pair.count == 2, !pair[0].isEmpty || !pair[1].isEmpty else { return nil }
            let a = String(pair[0])
            let b = String(pair[1])
            self.kind = .dm
            self.channelId = "\(a)|\(b)"
            self.peerId = a == selfId ? b : a
        } else if key.hasPrefix("g:") {
            let gid = String(key.dropFirst(2))
            guard !gid.isEmpty else { return nil }
            self.kind = .group
            self.channelId = gid
            self.peerId = gid
        } else {
            return nil
        }
    }

    private init(kind: Kind, channelId: String, peerId: String) {
        self.kind = kind
        self.channelId = channelId
        self.peerId = peerId
    }
}
