import ChirpProtos
import Foundation

/// 会话列表的数据面:按对端聚合 DM(最后一条预览、未读数、最新时间戳)。
/// 协议包的 `MemoryMessageStore` 只按频道存消息、不知道「有哪些频道」,
/// 会话列表是应用层状态——归这里,不扩协议缝(web_companion 同样在应用
/// 层维护 conversations)。
public final class SessionIndex {
    public struct Summary: Equatable {
        public let peerId: String
        public let lastMessage: String
        public let lastTimestamp: Int64
        public let unread: Int

        public init(
            peerId: String, lastMessage: String, lastTimestamp: Int64, unread: Int
        ) {
            self.peerId = peerId
            self.lastMessage = lastMessage
            self.lastTimestamp = lastTimestamp
            self.unread = unread
        }
    }

    private struct Entry {
        var lastMessage: String
        var lastTimestamp: Int64
        var unread: Int
    }

    private let lock = NSLock()
    private var entries: [String: Entry] = [:]

    public init() {}

    /// DM 频道 id 与管线同款拼法:排序对 `a|b`,两侧落同一桶。
    public static func dmChannelId(_ a: String, _ b: String) -> String {
        [a, b].sorted().joined(separator: "|")
    }

    /// 收到一条 DM(CHAT_MESSAGE_NOTIFY 渲染路径调用)。空 senderId 丢弃。
    public func recordIncoming(_ message: Chirp_Chat_ChatMessage) {
        let peer = message.senderID
        guard !peer.isEmpty else { return }
        let text = String(data: message.content, encoding: .utf8) ?? ""
        lock.lock()
        defer { lock.unlock() }
        var entry = entries[peer] ?? Entry(lastMessage: "", lastTimestamp: 0, unread: 0)
        entry.lastMessage = text
        entry.lastTimestamp = message.timestamp
        entry.unread += 1
        entries[peer] = entry
    }

    /// 自己发出且服务端已受理的一条(未读不变)。
    public func recordOutgoing(peerId: String, content: String, timestamp: Int64) {
        lock.lock()
        defer { lock.unlock() }
        var entry = entries[peerId] ?? Entry(lastMessage: "", lastTimestamp: 0, unread: 0)
        entry.lastMessage = content
        entry.lastTimestamp = timestamp
        entries[peerId] = entry
    }

    /// 打开该会话即清零未读(store 的 markRead 管持久面,这里管界面面)。
    public func markRead(peerId: String) {
        lock.lock()
        defer { lock.unlock() }
        entries[peerId]?.unread = 0
    }

    /// 最新在前(时间戳降序,平局按 peerId 稳定)。
    public func summaries() -> [Summary] {
        lock.lock()
        defer { lock.unlock() }
        return entries
            .map { Summary(peerId: $0.key, lastMessage: $0.value.lastMessage,
                           lastTimestamp: $0.value.lastTimestamp, unread: $0.value.unread) }
            .sorted { ($0.lastTimestamp, $0.peerId) > ($1.lastTimestamp, $1.peerId) }
    }

    public func summary(peerId: String) -> Summary? {
        lock.lock()
        defer { lock.unlock() }
        guard let e = entries[peerId] else { return nil }
        return Summary(peerId: peerId, lastMessage: e.lastMessage,
                       lastTimestamp: e.lastTimestamp, unread: e.unread)
    }

    public func unreadTotal() -> Int {
        lock.lock()
        defer { lock.unlock() }
        return entries.values.reduce(0) { $0 + $1.unread }
    }
}
