import Foundation

/// 好友名册的客户端镜像(P4c,web friend_store.ts 同语义):服务端权威
/// ——登录拉 GET_FRIEND_LIST/GET_PENDING_REQUESTS,notify 保持同步;
/// 仅 pendingOut 纯本地(服务端尚无出向查询,刷新即丢,同 web)。
/// friends/pendingOut 有序(字典序),pendingIn 保服务端到达序。
public final class FriendIndex {
    public struct PendingIn: Equatable {
        public let requestId: String
        public let fromUserId: String

        public init(requestId: String, fromUserId: String) {
            self.requestId = requestId
            self.fromUserId = fromUserId
        }
    }

    private let lock = NSLock()
    private var friendIds: [String] = []
    private var incoming: [PendingIn] = []
    private var outgoing: [String] = []

    public init() {}

    /// GET_FRIEND_LIST 应答:整表换入(排序;内容相同短路不换引用)。
    public func replaceFriends(_ ids: [String]) {
        let sorted = ids.sorted()
        lock.lock()
        defer { lock.unlock() }
        if friendIds == sorted { return }
        friendIds = sorted
    }

    /// FRIEND_ACCEPTED_NOTIFY / 本地接受:幂等插入。
    public func addFriend(_ userId: String) {
        lock.lock()
        defer { lock.unlock() }
        if friendIds.contains(userId) { return }
        friendIds.append(userId)
        friendIds.sort()
    }

    /// FRIEND_REMOVED_NOTIFY / 本地移除或拉黑:幂等删除。
    public func removeFriend(_ userId: String) {
        lock.lock()
        defer { lock.unlock() }
        friendIds.removeAll { $0 == userId }
    }

    /// GET_PENDING_REQUESTS 应答:整表换入(保到达序)。
    public func replacePendingIn(_ requests: [PendingIn]) {
        lock.lock()
        defer { lock.unlock() }
        incoming = requests
    }

    /// FRIEND_REQUEST_NOTIFY:按 requestId 去重追加。
    public func addPendingIn(requestId: String, fromUserId: String) {
        lock.lock()
        defer { lock.unlock() }
        guard !incoming.contains(where: { $0.requestId == requestId }) else { return }
        incoming.append(PendingIn(requestId: requestId, fromUserId: fromUserId))
    }

    /// 本地接受/拒绝后出队(应答请求)。
    public func resolvePending(requestId: String) {
        lock.lock()
        defer { lock.unlock() }
        incoming.removeAll { $0.requestId == requestId }
    }

    /// 本地记录我方发出的申请:已是好友或已申请过则不重复。
    public func addPendingOut(_ userId: String) {
        lock.lock()
        defer { lock.unlock() }
        if outgoing.contains(userId) || friendIds.contains(userId) { return }
        outgoing.append(userId)
        outgoing.sort()
    }

    // ---- 读面(壳层直读) ------------------------------------------------------

    public func friends() -> [String] {
        lock.lock()
        defer { lock.unlock() }
        return friendIds
    }

    public func pendingIn() -> [PendingIn] {
        lock.lock()
        defer { lock.unlock() }
        return incoming
    }

    public func pendingOut() -> [String] {
        lock.lock()
        defer { lock.unlock() }
        return outgoing
    }

    /// 切号/登出:整体丢弃旧账号名册。
    public func reset() {
        lock.lock()
        defer { lock.unlock() }
        friendIds = []
        incoming = []
        outgoing = []
    }
}
