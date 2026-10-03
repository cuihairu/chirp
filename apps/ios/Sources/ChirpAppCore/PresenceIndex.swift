import ChirpProtos
import Foundation

/// 对端在线状态镜像(P4c,web presence_store.ts 同语义):按 user_id 键控
/// 的快照,服务端不保证超时——断连 notify 丢失靠 70s TTL 兜底渲染
/// (presenceFresh 同款)。数据来源:PRESENCE_NOTIFY 增量 + GET_PRESENCE
/// 批量拉取。
public final class PresenceIndex {
    public struct Entry: Equatable {
        public let userId: String
        public let status: Chirp_Social_PresenceStatus
        public let statusMessage: String
        /// 本地到达时刻(ms)。
        public let atMs: Int64

        public init(
            userId: String, status: Chirp_Social_PresenceStatus,
            statusMessage: String, atMs: Int64
        ) {
            self.userId = userId
            self.status = status
            self.statusMessage = statusMessage
            self.atMs = atMs
        }
    }

    /// 快照陈旧阈值(web PRESENCE_TTL_MS 同值)。
    public static let ttlMs: Int64 = 70_000

    private let lock = NSLock()
    private var byUser: [String: Entry] = [:]

    public init() {}

    public func set(
        _ userId: String, status: Chirp_Social_PresenceStatus,
        statusMessage: String, atMs: Int64
    ) {
        guard !userId.isEmpty else { return }
        let entry = Entry(
            userId: userId, status: status, statusMessage: statusMessage,
            atMs: atMs)
        lock.lock()
        byUser[userId] = entry
        lock.unlock()
    }

    public func entry(_ userId: String) -> Entry? {
        lock.lock()
        defer { lock.unlock() }
        return byUser[userId]
    }

    /// 超过 70s 的快照按离线渲染(漏了 disconnect notify 的兜底)。
    public func isFresh(_ userId: String, nowMs: Int64) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard let entry = byUser[userId] else { return false }
        return nowMs - entry.atMs < Self.ttlMs
    }

    /// 删好友时顺手作废该用户的快照(名册已无此人,留着只会陈旧)。
    public func remove(_ userId: String) {
        lock.lock()
        defer { lock.unlock() }
        byUser.removeValue(forKey: userId)
    }

    public func reset() {
        lock.lock()
        defer { lock.unlock() }
        byUser = [:]
    }
}
