import ChirpProtos
import Foundation

/// 「正在输入」数据面(对齐 web typing_store:客户端 TTL 6s 过期,服务端
/// 也会广播 stop;自己上报的条目不入表)。
public final class TypingIndex {
    /// web TYPING_TTL_MS 同值。
    public static let ttlMs: Int64 = 6_000

    private let lock = NSLock()
    /// channelKey("type|id") → userId → 最近一次上报时刻。
    private var lastSeen: [String: [String: Int64]] = [:]

    public init() {}

    public static func channelKey(
        channelType: Chirp_Chat_ChannelType, channelId: String
    ) -> String {
        "\(channelType.rawValue)|\(channelId)"
    }

    public func record(_ indicator: Chirp_Chat_TypingIndicator, selfId: String) {
        guard indicator.userID != selfId, !indicator.userID.isEmpty else { return }
        lock.lock()
        defer { lock.unlock() }
        var channel = lastSeen[Self.channelKey(
            channelType: indicator.channelType, channelId: indicator.channelID)] ?? [:]
        if indicator.isTyping {
            channel[indicator.userID] = indicator.timestamp
        } else {
            channel.removeValue(forKey: indicator.userID)
        }
        lastSeen[Self.channelKey(
            channelType: indicator.channelType, channelId: indicator.channelID)] = channel
    }

    /// 该频道 TTL 内仍在输入的用户(已排自己;过期条目惰性清掉)。
    public func typists(
        channelType: Chirp_Chat_ChannelType, channelId: String, nowMs: Int64
    ) -> [String] {
        let key = Self.channelKey(channelType: channelType, channelId: channelId)
        lock.lock()
        defer { lock.unlock() }
        var channel = lastSeen[key] ?? [:]
        for (user, at) in channel where nowMs - at >= Self.ttlMs {
            channel.removeValue(forKey: user)
        }
        if channel.isEmpty {
            lastSeen.removeValue(forKey: key)
        } else {
            lastSeen[key] = channel
        }
        return Array(channel.keys)
    }
}
