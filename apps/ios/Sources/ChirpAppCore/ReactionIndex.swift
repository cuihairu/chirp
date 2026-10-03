import ChirpProtos
import Foundation

/// 第一阶段固定快捷反应集(web QUICK_REACTIONS 同款八枚)。
public enum QuickReactions {
    public static let all: [String] = ["👍", "❤️", "😂", "😮", "😢", "😡", "🎉", "👀"]
}

/// 消息快捷反应的数据面(对齐 web:纯 notify 驱动——历史消息不回填反应,
/// REACTION_ADDED/REMOVED_NOTIFY 与自己的 add/remove 应答共同维护)。
public final class ReactionIndex {
    public struct Tally: Equatable {
        public let emoji: String
        public let count: Int
        public let isMine: Bool

        public init(emoji: String, count: Int, isMine: Bool) {
            self.emoji = emoji
            self.count = count
            self.isMine = isMine
        }
    }

    /// 单 emoji 槽位;数组序=首次到达序(Swift 字典不保序,反应集八枚封顶,
    /// 线性查找无碍)。
    private struct Slot {
        var emoji: String
        var count: Int
        var mine: Bool
    }

    private let lock = NSLock()
    /// messageId → 槽位数组(首达序即展示序)。
    private var byMessage: [String: [Slot]] = [:]

    public init() {}

    /// 一条反应事件(服务端 notify 或自己的应答)。count 钳到 0,清零即删槽。
    public func record(
        messageId: String, emoji: String, userId: String, added: Bool, selfId: String
    ) {
        guard !messageId.isEmpty, !emoji.isEmpty else { return }
        lock.lock()
        defer { lock.unlock() }
        var slots = byMessage[messageId] ?? []
        guard let at = slots.firstIndex(where: { $0.emoji == emoji }) else {
            // remove 不引入新槽;add 首达即插入。
            if added { slots.append(Slot(emoji: emoji, count: 1, mine: userId == selfId)) }
            if !slots.isEmpty { byMessage[messageId] = slots }
            return
        }
        if added {
            slots[at].count += 1
            if userId == selfId { slots[at].mine = true }
        } else {
            slots[at].count -= 1
            if userId == selfId { slots[at].mine = false }
            if slots[at].count <= 0 { slots.remove(at: at) }
        }
        if slots.isEmpty {
            byMessage.removeValue(forKey: messageId)
        } else {
            byMessage[messageId] = slots
        }
    }

    /// 用服务端聚合覆盖该槽(ADD_REACTION_RESP 的 reaction 是真相——
    /// 服务端把 notify 只扇给频道其他成员,操作者不在扇出面,自己的加
    /// 反应只能靠应答聚合落地;web setReaction 同款)。
    public func applyAggregate(
        messageId: String, reaction: Chirp_Chat_MessageReaction, selfId: String
    ) {
        guard !messageId.isEmpty, !reaction.emoji.isEmpty, reaction.count > 0 else { return }
        lock.lock()
        defer { lock.unlock() }
        var slots = byMessage[messageId] ?? []
        let slot = Slot(
            emoji: reaction.emoji, count: Int(reaction.count), mine: reaction.reactedByMe)
        if let at = slots.firstIndex(where: { $0.emoji == reaction.emoji }) {
            slots[at] = slot
        } else {
            slots.append(slot)
        }
        byMessage[messageId] = slots
    }

    /// 该消息的反应计数(首达序)。
    public func tallies(messageId: String) -> [Tally] {
        lock.lock()
        defer { lock.unlock() }
        return (byMessage[messageId] ?? []).map {
            Tally(emoji: $0.emoji, count: $0.count, isMine: $0.mine)
        }
    }

    public func isMine(messageId: String, emoji: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return byMessage[messageId]?.first(where: { $0.emoji == emoji })?.mine ?? false
    }
}
