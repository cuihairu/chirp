import ChirpProtos
import Foundation

/// 会话列表的数据面(P4e 起按键寻址):按频道聚合 DM 与群(最后一条预览、
/// 未读数、最新时间戳),键与 web conversation key 同位。协议包的
/// `MemoryMessageStore` 只按频道存消息、不知道「有哪些频道」,会话列表是
/// 应用层状态——归这里,不扩协议缝(web_companion 同样在应用层维护
/// conversations)。
public final class SessionIndex {
    public struct Summary: Equatable {
        /// 导航键('p:a|b' / 'g:gid')。
        public let key: String
        public let kind: SessionChannel.Kind
        /// 对端用户 id(DM)/群 id(群)。
        public let peerId: String
        public let lastMessage: String
        public let lastTimestamp: Int64
        public let unread: Int

        public init(
            key: String, kind: SessionChannel.Kind, peerId: String,
            lastMessage: String, lastTimestamp: Int64, unread: Int
        ) {
            self.key = key
            self.kind = kind
            self.peerId = peerId
            self.lastMessage = lastMessage
            self.lastTimestamp = lastTimestamp
            self.unread = unread
        }
    }

    private struct Entry {
        var kind: SessionChannel.Kind
        var peerId: String
        var lastMessage: String
        var lastTimestamp: Int64
        var unread: Int
    }

    /// 登录身份:DM 记账要把入站消息折成 'p:'+排序对键(自己那一侧只有
    /// 本实例知道);服务在构造时传入。
    private let selfId: String
    private let lock = NSLock()
    private var entries: [String: Entry] = [:]

    public init(selfId: String) {
        self.selfId = selfId
    }

    /// DM 频道 id 与管线同款拼法:排序对 `a|b`,两侧落同一桶。
    public static func dmChannelId(_ a: String, _ b: String) -> String {
        [a, b].sorted().joined(separator: "|")
    }

    /// 收到一条消息(CHAT_MESSAGE_NOTIFY 渲染路径调用)。DM 空 senderId
    /// 丢弃;群消息按 message.channelID 归 'g:' 桶——**不再**按 sender 折成
    /// DM 会话(群消息的对端是群,不是发信人)。
    public func recordIncoming(_ message: Chirp_Chat_ChatMessage) {
        let text = String(data: message.content, encoding: .utf8) ?? ""
        let channel: SessionChannel
        switch message.channelType {
        case .guild:
            guard !message.channelID.isEmpty else { return }
            channel = .group(message.channelID)
        default:
            // .private 走 DM;其余频道类型(team/world…)伴侣面不用,维持
            // 既有 DM 口径按 sender 归桶。
            let peer = message.senderID
            guard !peer.isEmpty else { return }
            channel = .dm(selfId: selfId, peerId: peer)
        }
        lock.lock()
        defer { lock.unlock() }
        var entry =
            entries[channel.key]
            ?? Entry(
                kind: channel.kind, peerId: channel.peerId,
                lastMessage: "", lastTimestamp: 0, unread: 0)
        entry.lastMessage = text
        entry.lastTimestamp = message.timestamp
        entry.unread += 1
        entries[channel.key] = entry
    }

    /// 自己发出且服务端已受理的一条(未读不变)。
    public func recordOutgoing(
        key: String, peerId: String, content: String, timestamp: Int64
    ) {
        lock.lock()
        defer { lock.unlock() }
        var entry =
            entries[key]
            ?? Entry(
                kind: key.hasPrefix("g:") ? .group : .dm, peerId: peerId,
                lastMessage: "", lastTimestamp: 0, unread: 0)
        entry.lastMessage = content
        entry.lastTimestamp = timestamp
        entries[key] = entry
    }

    /// 群名单引导(GET_USER_GROUPS 逐群调):无预览的群先落一行——空行在
    /// 收到消息前就在列表里(web refreshGroups upsert 同语义);已有条目只
    /// 保留预览不动。
    public func ensureGroup(groupId: String) {
        guard !groupId.isEmpty else { return }
        let key = SessionChannel.group(groupId).key
        lock.lock()
        defer { lock.unlock() }
        if entries[key] == nil {
            entries[key] = Entry(
                kind: .group, peerId: groupId,
                lastMessage: "", lastTimestamp: 0, unread: 0)
        }
    }

    /// 退群/被踢:丢弃该会话(预览与未读一并)。未知键无副作用。
    public func remove(key: String) {
        lock.lock()
        defer { lock.unlock() }
        entries.removeValue(forKey: key)
    }

    /// 打开该会话即清零未读(store 的 markRead 管持久面,这里管界面面)。
    public func markRead(key: String) {
        lock.lock()
        defer { lock.unlock() }
        entries[key]?.unread = 0
    }

    /// 最新在前(时间戳降序,平局按键稳定)。
    public func summaries() -> [Summary] {
        lock.lock()
        defer { lock.unlock() }
        return entries
            .map {
                Summary(
                    key: $0.key, kind: $0.value.kind, peerId: $0.value.peerId,
                    lastMessage: $0.value.lastMessage,
                    lastTimestamp: $0.value.lastTimestamp, unread: $0.value.unread)
            }
            .sorted { ($0.lastTimestamp, $0.key) > ($1.lastTimestamp, $1.key) }
    }

    public func summary(key: String) -> Summary? {
        lock.lock()
        defer { lock.unlock() }
        guard let e = entries[key] else { return nil }
        return Summary(
            key: key, kind: e.kind, peerId: e.peerId,
            lastMessage: e.lastMessage, lastTimestamp: e.lastTimestamp, unread: e.unread)
    }

    public func unreadTotal() -> Int {
        lock.lock()
        defer { lock.unlock() }
        return entries.values.reduce(0) { $0 + $1.unread }
    }
}
