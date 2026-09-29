import ChirpProtos
import Foundation

/// Send-shape parameters (port of Kotlin SendOptions).
public struct SendOptions {
    public let channelType: Chirp_Chat_ChannelType
    public let channelId: String?
    public let receiverId: String?
    public let replyToMessageId: String
    public let msgType: Chirp_Chat_MsgType

    public init(
        channelType: Chirp_Chat_ChannelType,
        channelId: String? = nil,
        receiverId: String? = nil,
        replyToMessageId: String = "",
        msgType: Chirp_Chat_MsgType = .text
    ) {
        self.channelType = channelType
        self.channelId = channelId
        self.receiverId = receiverId
        self.replyToMessageId = replyToMessageId
        self.msgType = msgType
    }
}

/// Stand-in for Kotlin `IllegalArgumentException` at the send-path argument
/// validation points (Swift has no direct equivalent to throw).
public struct ChirpArgumentError: Error, CustomStringConvertible {
    public let message: String
    public init(_ message: String) { self.message = message }
    public var description: String { message }
}

// ---- seams (port of Kotlin Hooks.kt) ---------------------------------------
//
// Hook surface and throw semantics: Kotlin wraps every hook call in
// runCatching/catch so one throwing hook degrades to the documented outcome
// (drop / block / decline / skip). Swift cannot observe a throw through a
// non-throwing protocol requirement, so every hook is declared `throws` and
// every call site uses `try?` (or do/catch where the throw maps to a distinct
// outcome). That keeps the Kotlin runtime vectors runnable one-for-one.

/// Rewrites/drops messages on the way out and in. nil from onBeforeSend/
/// onBeforeReceive drops the message; a throw drops it too (Kotlin caught
/// exceptions mapped to null).
public protocol MessageInterceptor: AnyObject {
    /// Return nil (or throw) to block the outgoing message.
    func onBeforeSend(_ request: Chirp_Chat_SendMessageRequest) throws -> Chirp_Chat_SendMessageRequest?
    func onAfterSend(_ request: Chirp_Chat_SendMessageRequest) throws
    /// Return nil (or throw) to drop the incoming message.
    func onBeforeReceive(_ message: Chirp_Chat_ChatMessage) throws -> Chirp_Chat_ChatMessage?
    func onAfterReceive(_ message: Chirp_Chat_ChatMessage) throws
}

extension MessageInterceptor {
    public func onBeforeSend(
        _ request: Chirp_Chat_SendMessageRequest
    ) throws -> Chirp_Chat_SendMessageRequest? { request }
    public func onAfterSend(_ request: Chirp_Chat_SendMessageRequest) throws {}
    public func onBeforeReceive(
        _ message: Chirp_Chat_ChatMessage
    ) throws -> Chirp_Chat_ChatMessage? { message }
    public func onAfterReceive(_ message: Chirp_Chat_ChatMessage) throws {}
}

/// Token source for pipeline login (port of Kotlin AuthProvider).
public protocol AuthProvider: AnyObject {
    func getToken() -> String
    /// One renewal chance after an AUTH_FAILED login round; nil = give up.
    func renewToken() -> String?
    func onAuthResult(_ code: Chirp_Common_ErrorCode, userId: String) throws
}

extension AuthProvider {
    public func renewToken() -> String? { nil }
    public func onAuthResult(_ code: Chirp_Common_ErrorCode, userId: String) throws {}
}

/// Message archive seam (port of Kotlin MessageStore). `load` returns
/// newest-first; `beforeTimestamp == 0` means no cursor.
public protocol MessageStore: AnyObject {
    func save(_ message: Chirp_Chat_ChatMessage) throws
    func load(
        channelType: Chirp_Chat_ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Int64
    ) -> [Chirp_Chat_ChatMessage]
    func getUnreadCount(channelType: Chirp_Chat_ChannelType, channelId: String) -> Int
    func markRead(channelType: Chirp_Chat_ChannelType, channelId: String) throws
    func cleanup(olderThanMs: Int64) throws
}

extension MessageStore {
    public func getUnreadCount(channelType: Chirp_Chat_ChannelType, channelId: String) -> Int { 0 }
    public func markRead(channelType: Chirp_Chat_ChannelType, channelId: String) throws {}
    public func cleanup(olderThanMs: Int64) throws {}
}

/// In-memory per-channel ring buffer, newest kept (port of Kotlin
/// MemoryMessageStore). Bucket key is "channelTypeValue|channelId" for both
/// directions; the sent-direction marker is an empty messageID.
public final class MemoryMessageStore: MessageStore {
    private let maxPerChannel: Int
    private let lock = NSLock()
    // Oldest-first; front is the eviction end.
    private var channels: [String: [Chirp_Chat_ChatMessage]] = [:]

    public init(maxPerChannel: Int = 200) {
        self.maxPerChannel = maxPerChannel
    }

    private static func key(_ channelType: Chirp_Chat_ChannelType, _ channelId: String) -> String {
        "\(channelType.rawValue)|\(channelId)"
    }

    public func save(_ message: Chirp_Chat_ChatMessage) throws {
        let key = Self.key(message.channelType, message.channelID)
        lock.lock()
        defer { lock.unlock() }
        var ring = channels[key] ?? []
        ring.append(message)
        if ring.count > maxPerChannel {
            ring.removeFirst(ring.count - maxPerChannel)
        }
        channels[key] = ring
    }

    public func load(
        channelType: Chirp_Chat_ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Int64 = 0
    ) -> [Chirp_Chat_ChatMessage] {
        let key = Self.key(channelType, channelId)
        lock.lock()
        defer { lock.unlock() }
        return (channels[key] ?? [])
            .reversed()
            .filter { beforeTimestamp == 0 || $0.timestamp < beforeTimestamp }
            .prefix(limit)
            .map { $0 }
    }

    public func cleanup(olderThanMs: Int64) throws {
        lock.lock()
        defer { lock.unlock() }
        for key in channels.keys {
            channels[key]?.removeAll { $0.timestamp < olderThanMs }
            if channels[key]?.isEmpty == true {
                channels.removeValue(forKey: key)
            }
        }
    }
}

/// Pipeline event fan-out (port of Kotlin ChatEventListener); all methods are
/// optional via default implementations, and a throwing method is isolated by
/// the pipeline (`try?` per call — Kotlin ran each listener under runCatching).
public protocol ChatEventListener: AnyObject {
    func onConnectionStateChanged(_ state: ConnState) throws
    func onLoginResult(_ code: Chirp_Common_ErrorCode, userId: String) throws
    func onKicked(reason: String) throws
    func onReconnecting(attempt: Int, delayMs: Int64) throws
    func onReconnected() throws
    func onMessageReceived(_ message: Chirp_Chat_ChatMessage) throws
    func onDevicesPresence(_ devices: [Chirp_Auth_DevicePresence]) throws
    func onLoginDevices(_ devices: [Chirp_Auth_DevicePresence]) throws
}

extension ChatEventListener {
    public func onConnectionStateChanged(_ state: ConnState) throws {}
    public func onLoginResult(_ code: Chirp_Common_ErrorCode, userId: String) throws {}
    public func onKicked(reason: String) throws {}
    public func onReconnecting(attempt: Int, delayMs: Int64) throws {}
    public func onReconnected() throws {}
    public func onMessageReceived(_ message: Chirp_Chat_ChatMessage) throws {}
    public func onDevicesPresence(_ devices: [Chirp_Auth_DevicePresence]) throws {}
    public func onLoginDevices(_ devices: [Chirp_Auth_DevicePresence]) throws {}
}

/// Local slash command (port of Kotlin CommandHandler). Returning false (or
/// throwing) declines, passing the command to the next same-name handler; the
/// last decline blocks the send locally.
public protocol CommandHandler: AnyObject {
    var name: String { get }
    var usage: String { get }
    func execute(args: String, senderId: String) throws -> Bool
}

extension CommandHandler {
    public var usage: String { "/\(name)" }
}
