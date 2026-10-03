import ChirpProtos
import Foundation

/// 群名单的客户端镜像(P4e,web conversation 的群侧元数据同位):服务端
/// 权威——GET_USER_GROUPS 整表换入,notify 触发重拉;退群/被踢本地移除。
/// 预览与未读不在这里(归 SessionIndex),这里只有面板与标题要用的元数据。
public final class GroupIndex {
    public struct Entry: Equatable {
        public let groupId: String
        public let name: String
        public let ownerId: String
        public let memberCount: Int32

        public init(groupId: String, name: String, ownerId: String, memberCount: Int32) {
            self.groupId = groupId
            self.name = name
            self.ownerId = ownerId
            self.memberCount = memberCount
        }

        public init(info: Chirp_Chat_GroupInfo) {
            self.init(
                groupId: info.groupID, name: info.groupName,
                ownerId: info.ownerID, memberCount: info.memberCount)
        }
    }

    private let lock = NSLock()
    private var byId: [String: Entry] = [:]

    public init() {}

    /// GET_USER_GROUPS 应答:整表换入(按 groupId 字典序;内容相同短路不换
    /// 引用——FriendIndex.replaceFriends 同款)。
    public func replace(_ entries: [Entry]) {
        var next: [String: Entry] = [:]
        for entry in entries where !entry.groupId.isEmpty {
            next[entry.groupId] = entry
        }
        lock.lock()
        defer { lock.unlock() }
        if next == byId { return }
        byId = next
    }

    /// 退群/被踢:本地移除该群。未知 id 无副作用。
    public func remove(groupId: String) {
        lock.lock()
        defer { lock.unlock() }
        byId.removeValue(forKey: groupId)
    }

    // ---- 读面(壳层直读) ------------------------------------------------------

    public func entries() -> [Entry] {
        lock.lock()
        defer { lock.unlock() }
        return byId.values.sorted { $0.groupId < $1.groupId }
    }

    public func entry(groupId: String) -> Entry? {
        lock.lock()
        defer { lock.unlock() }
        return byId[groupId]
    }

    /// 标题面:名单里有名字用名字,否则回落群 id(web 空 groupName 同款回落)。
    public func name(groupId: String) -> String {
        guard let entry = entry(groupId: groupId), !entry.name.isEmpty else {
            return groupId
        }
        return entry.name
    }

    /// 切号/登出:整体丢弃旧账号名单。
    public func reset() {
        lock.lock()
        defer { lock.unlock() }
        byId = [:]
    }
}
