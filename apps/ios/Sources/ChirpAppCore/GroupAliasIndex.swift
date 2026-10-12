import Foundation

/// 群内昵称快照(对齐 web:groupId → userId → alias;缺键 = 未设或已清除)。
/// 渲染两处消费:成员列表行 + 群聊消息发送者名(ChatMessage 无 alias 字段,
/// 映射走这份快照而非改协议)。两路维护——loadGroupMembers 名册行整群重建、
/// 2124 通知折叠单点变更(空串 = 清除即移除);名册未载入的群不落快照。
public final class GroupAliasIndex {
    private let lock = NSLock()
    private var byGroup: [String: [String: String]] = [:]

    public init() {}

    /// 读群内昵称;未设或该群名册未载入回空串(发送者名映射按此回退本名)。
    public func aliasOf(groupId: String, userId: String) -> String {
        lock.lock()
        defer { lock.unlock() }
        return byGroup[groupId]?[userId] ?? ""
    }

    /// 名册为准重建整群快照(alias 非空才落,loadGroupMembers 应答行同款)。
    public func replace(groupId: String, members: [(userId: String, alias: String)]) {
        lock.lock()
        defer { lock.unlock() }
        var aliases: [String: String] = [:]
        for member in members where !member.alias.isEmpty {
            aliases[member.userId] = member.alias
        }
        byGroup[groupId] = aliases
    }

    /// 2124 折叠:空串 = 清除(移除,发送者名回退本名);该群快照未载入则
    /// 忽略(web onGroupMemberAliasUpdated 同款,等名册拉取后才有基线)。
    public func fold(groupId: String, userId: String, alias: String) {
        lock.lock()
        defer { lock.unlock() }
        guard byGroup[groupId] != nil else { return }
        if alias.isEmpty {
            byGroup[groupId]?.removeValue(forKey: userId)
        } else {
            byGroup[groupId]?[userId] = alias
        }
    }

    /// 退群/被踢时随群行清快照(iOS 服务面同槽清理,防陈旧映射滞留)。
    public func remove(groupId: String) {
        lock.lock()
        defer { lock.unlock() }
        byGroup.removeValue(forKey: groupId)
    }

    public func reset() {
        lock.lock()
        defer { lock.unlock() }
        byGroup = [:]
    }
}
