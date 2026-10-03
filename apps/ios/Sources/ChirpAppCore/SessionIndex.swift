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

    /// 快照缝(P6 持久化):壳层给字节读写(DeviceIdentity 的 load/save 闭包
    /// 同款手法),JSON 编码与校验归本类型。**save 回调不得重入本索引**
    /// (变更在锁内直接落盘)。
    public struct SnapshotIO {
        public let load: () -> Data?
        public let save: (Data) -> Void

        public init(load: @escaping () -> Data?, save: @escaping (Data) -> Void) {
            self.load = load
            self.save = save
        }
    }

    /// 落盘线格式(私有):v=1 envelope。kind/peerId 不存——键即身份,载入时
    /// 经 SessionChannel 反解,格式演进只加版本位。
    private struct Snapshot: Codable {
        var v: Int
        var rows: [Row]
    }

    private struct Row: Codable {
        var key: String
        var lastMessage: String
        var lastTimestamp: Int64
        var unread: Int
    }

    /// 登录身份:DM 记账要把入站消息折成 'p:'+排序对键(自己那一侧只有
    /// 本实例知道);服务在构造时传入。
    private let selfId: String
    /// 快照缝:nil = 纯内存(单测/无持久化面);非 nil 时构造即回灌、每次
    /// 变更即落盘。
    private let snapshotIO: SnapshotIO?
    private let lock = NSLock()
    private var entries: [String: Entry] = [:]

    public init(selfId: String, snapshotIO: SnapshotIO? = nil) {
        self.selfId = selfId
        self.snapshotIO = snapshotIO
        // 构造期回灌(stored 属性已齐,方法调用合法):预览是可重建的缓存面,
        // 破损/异版本快照一律当全新开始,不报错。
        if let data = snapshotIO?.load() {
            hydrate(from: data)
        }
    }

    private func hydrate(from data: Data) {
        guard let snap = try? JSONDecoder().decode(Snapshot.self, from: data),
            snap.v == 1
        else { return }
        for row in snap.rows {
            guard let channel = SessionChannel(key: row.key, selfId: selfId) else {
                continue  // 畸形键/未知前缀
            }
            // DM 行必须含自己这一侧:快照按用户分键,这一门再拦一层串档
            // (selfId 空串的测试面不启用)。
            if channel.kind == .dm, !selfId.isEmpty {
                let sides = channel.channelId.split(
                    separator: "|", omittingEmptySubsequences: false)
                guard sides.count == 2, sides.contains(where: { String($0) == selfId })
                else { continue }
            }
            entries[channel.key] = Entry(
                kind: channel.kind, peerId: channel.peerId,
                lastMessage: row.lastMessage, lastTimestamp: row.lastTimestamp,
                unread: max(0, row.unread))
        }
    }

    /// 变更后落盘(锁内调用):行按键序稳定输出,编码失败静默丢——预览可由
    /// 消息与服务端历史重建,不值得为此报错。
    private func persistLocked() {
        guard let io = snapshotIO else { return }
        let snap = Snapshot(
            v: 1,
            rows: entries.sorted { $0.key < $1.key }.map {
                Row(
                    key: $0.key, lastMessage: $0.value.lastMessage,
                    lastTimestamp: $0.value.lastTimestamp, unread: $0.value.unread)
            })
        guard let data = try? JSONEncoder().encode(snap) else { return }
        io.save(data)
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
        persistLocked()
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
        persistLocked()
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
            persistLocked()
        }
    }

    /// 退群/被踢/列表删除:丢弃该会话(预览与未读一并)。未知键无副作用
    /// (也不落盘)。
    public func remove(key: String) {
        lock.lock()
        defer { lock.unlock() }
        if entries.removeValue(forKey: key) != nil {
            persistLocked()
        }
    }

    /// 打开该会话即清零未读(store 的 markRead 管持久面,这里管界面面)。
    /// 未知键/已清零不落盘。
    public func markRead(key: String) {
        lock.lock()
        defer { lock.unlock() }
        guard var entry = entries[key], entry.unread != 0 else { return }
        entry.unread = 0
        entries[key] = entry
        persistLocked()
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
