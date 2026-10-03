import Foundation

/// 组队面的客户端镜像(P4d,web party_store.ts 同语义):服务端权威、
/// 快照式同步——PARTY_STATE_CHANGED 携带全量 PartyInfo 发给每个成员
/// (含操作者),镜像只 apply 快照,kick/disband 清空。邀请镜像入向队列
/// (服务端无出向查询,我发出的邀请不跟踪,web 同口径)。
public final class PartyIndex {
    public struct Member: Equatable {
        public let userId: String
        public let ready: Bool

        public init(userId: String, ready: Bool) {
            self.userId = userId
            self.ready = ready
        }
    }

    /// 服务端 PartyInfo 的窄拷贝(UI 渲染面)。
    public struct Snapshot: Equatable {
        public let partyId: String
        public let leaderId: String
        public let maxMembers: Int32
        public let members: [Member]

        public init(
            partyId: String, leaderId: String, maxMembers: Int32, members: [Member]
        ) {
            self.partyId = partyId
            self.leaderId = leaderId
            self.maxMembers = maxMembers
            self.members = members
        }
    }

    public struct Invite: Equatable {
        public let inviteId: String
        public let fromUserId: String
        public let partyId: String

        public init(inviteId: String, fromUserId: String, partyId: String) {
            self.inviteId = inviteId
            self.fromUserId = fromUserId
            self.partyId = partyId
        }
    }

    private let lock = NSLock()
    private var partyBox: Snapshot?
    private var inviteList: [Invite] = []

    public init() {}

    /// 整快照换入(STATE_CHANGED / 创建 / 接受邀请的应答)。
    public func apply(_ snapshot: Snapshot) {
        lock.lock()
        partyBox = snapshot
        lock.unlock()
    }

    /// 离开/被踢/解散都落这里。
    public func clear() {
        lock.lock()
        partyBox = nil
        lock.unlock()
    }

    /// INVITE_NOTIFY:按 inviteId 去重追加。
    public func addInvite(_ invite: Invite) {
        lock.lock()
        defer { lock.unlock() }
        guard !inviteList.contains(where: { $0.inviteId == invite.inviteId }) else { return }
        inviteList.append(invite)
    }

    /// 接受/拒绝后出队。
    public func removeInvite(inviteId: String) {
        lock.lock()
        defer { lock.unlock() }
        inviteList.removeAll { $0.inviteId == inviteId }
    }

    /// 换号/重登不得泄漏上一账号的邀请。
    public func resetInvites() {
        lock.lock()
        inviteList = []
        lock.unlock()
    }

    // ---- 读面(壳层直读) ------------------------------------------------------

    public func party() -> Snapshot? {
        lock.lock()
        defer { lock.unlock() }
        return partyBox
    }

    public func invites() -> [Invite] {
        lock.lock()
        defer { lock.unlock() }
        return inviteList
    }

    /// 当前快照的队长是否是自己。
    public func isLeader(userId: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return partyBox?.leaderId == userId
    }

    /// 自己的成员行(未入队返回 nil)。
    public func selfMember(userId: String) -> Member? {
        lock.lock()
        defer { lock.unlock() }
        return partyBox?.members.first { $0.userId == userId }
    }
}
