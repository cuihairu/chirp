import ChirpProtos
import Foundation
import SwiftProtobuf

/// Chat pipeline over a ChatConnection (port of ChatPipeline.kt): login with
/// token renewal, send-side validation and local slash commands, interceptor
/// hooks, message archive, and server-push fan-out.
///
/// Kotlin → Swift mapping:
///   - `CompletableFuture<T>` → `Promise<T>` (this package): thenApply →
///     `map`, thenCompose → `flatMap`, handle → `handle`.
///   - `synchronized(lock)` → `NSRecursiveLock` (login re-enters through the
///     provider seam, close re-enters through synchronous down events).
///   - `@Volatile var interceptor/provider/store` → lock-guarded accessors
///     (Swift has no volatile; visibility, not atomicity, is what Kotlin
///     relied on).
///   - Kotlin's runCatching around every hook call → hook methods declared
///     `throws` + `try?` at the call sites (see Hooks.swift).
public final class ChatPipeline {
    private let conn: ChatConnection
    private let selfId: () -> String
    private let deviceId: () -> String

    private let lock = NSRecursiveLock()
    private var listeners: [any ChatEventListener] = []
    private var commands: [any CommandHandler] = []
    private var unsubs: [() -> Void] = []
    private var interceptorBox: (any MessageInterceptor)?
    private var providerBox: (any AuthProvider)?
    private var storeBox: (any MessageStore)?

    public var interceptor: (any MessageInterceptor)? {
        get { withLock { interceptorBox } }
        set { withLock { interceptorBox = newValue } }
    }

    public var provider: (any AuthProvider)? {
        get { withLock { providerBox } }
        set { withLock { providerBox = newValue } }
    }

    public var store: (any MessageStore)? {
        get { withLock { storeBox } }
        set { withLock { storeBox = newValue } }
    }

    public init(
        conn: ChatConnection,
        selfId: @escaping () -> String,
        deviceId: @escaping () -> String
    ) {
        self.conn = conn
        self.selfId = selfId
        self.deviceId = deviceId
    }

    /// Wires the push handlers and connection listeners. Idempotent: a second
    /// start without stop is a no-op (matching the Kotlin guard).
    public func start() {
        withLock {
            if !unsubs.isEmpty { return }
        }
        // Subscription calls touch the connection lock; register outside the
        // pipeline lock so the lock order stays conn → pipeline everywhere.
        let u1 = conn.onNotify(msgId: .chatMessageNotify) { [weak self] body in
            self?.onIncoming(body)
        }
        let u2 = conn.onNotify(msgId: .kickNotify) { [weak self] body in
            self?.onKickBody(body)
        }
        let u3 = conn.onNotify(msgId: .devicesPresenceNotify) { [weak self] body in
            self?.onDevicesPresenceBody(body)
        }
        let u4 = conn.onStateChange { [weak self] state in
            guard let self = self else { return }
            for listener in self.snapshotListeners() {
                try? listener.onConnectionStateChanged(state)
            }
        }
        let u5 = conn.onReconnecting { [weak self] attempt, delayMs in
            guard let self = self else { return }
            for listener in self.snapshotListeners() {
                try? listener.onReconnecting(attempt: attempt, delayMs: delayMs)
            }
        }
        let u6 = conn.onReconnected { [weak self] in
            guard let self = self else { return }
            for listener in self.snapshotListeners() {
                try? listener.onReconnected()
            }
        }
        let fresh = [u1, u2, u3, u4, u5, u6]
        let redundant: [() -> Void]? = withLock {
            if unsubs.isEmpty {
                unsubs = fresh
                return nil
            }
            return fresh
        }
        // A concurrent start won the race: unwind our duplicate wiring.
        for u in redundant ?? [] { u() }
    }

    /// Unwires everything from start(); safe to call repeatedly and to
    /// start() again afterwards.
    public func stop() {
        let subs: [() -> Void] = withLock {
            let taken = unsubs
            unsubs = []
            return taken
        }
        for u in subs { u() }
    }

    public func addListener(_ listener: any ChatEventListener) -> () -> Void {
        withLock { listeners.append(listener) }
        return { [weak self] in
            guard let self = self else { return }
            self.withLock {
                self.listeners.removeAll { $0 === listener }
            }
        }
    }

    public func registerCommand(_ command: any CommandHandler) -> () -> Void {
        withLock { commands.append(command) }
        return { [weak self] in
            guard let self = self else { return }
            self.withLock {
                self.commands.removeAll { $0 === command }
            }
        }
    }

    // ---- login ---------------------------------------------------------------

    /// Login chain: explicit token → provider token → userId as guest token;
    /// one AUTH_FAILED renewal chance through the provider; the terminal code
    /// fans out to listeners and the provider. Completes with the final
    /// ErrorCode (never errors).
    public func login(userId: String, token: String? = nil) -> Promise<Chirp_Common_ErrorCode> {
        let firstToken = token ?? provider?.getToken() ?? userId
        return loginRound(firstToken)
            .flatMap { [weak self] code -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self, code == .authFailed,
                    let fresh = self.provider?.renewToken()
                else {
                    return Promise<Chirp_Common_ErrorCode>.completed(code)
                }
                return self.loginRound(fresh)
            }
            .map { [weak self] code in
                self?.terminalAuth(code, userId: userId)
                return code
            }
    }

    private func terminalAuth(_ code: Chirp_Common_ErrorCode, userId: String) {
        for listener in snapshotListeners() {
            try? listener.onLoginResult(code, userId: userId)
        }
        if let provider = provider {
            try? provider.onAuthResult(code, userId: userId)
        }
    }

    private func loginRound(_ token: String) -> Promise<Chirp_Common_ErrorCode> {
        var request = Chirp_Auth_LoginRequest()
        request.token = token
        request.deviceID = deviceId()
        // Kotlin reports "android"; this port runs on iOS.
        request.platform = "ios"
        return conn.request(spec: MsgSpecs.login, body: request).map { [weak self] response in
            if response.code == .ok {
                // Login success is what proves the route: reset the reconnect
                // ladder here (opening the socket alone proves little).
                self?.conn.resetBackoff()
                if !response.onlineDevices.isEmpty {
                    for listener in self?.snapshotListeners() ?? [] {
                        try? listener.onLoginDevices(response.onlineDevices)
                    }
                }
            }
            return response.code
        }
    }

    // ---- send ----------------------------------------------------------------

    /// Send path order (fixed by the Kotlin vectors): connection state first,
    /// then argument validation, then local slash commands, interceptor,
    /// archive, wire.
    public func send(
        options: SendOptions,
        content: String
    ) -> Promise<Chirp_Chat_SendMessageResponse> {
        // State check precedes argument validation: a closed connection
        // reports CLOSED even for an otherwise-invalid request.
        if conn.state != .connected {
            return Promise.failed(RequestError(.closed))
        }
        guard !content.isEmpty else {
            return Promise.failed(ChirpArgumentError("content must not be empty"))
        }
        switch options.channelType {
        case .`private`:
            guard options.receiverId != nil else {
                return Promise.failed(ChirpArgumentError("private send needs receiverId"))
            }
        default:
            guard options.channelId != nil else {
                return Promise.failed(
                    ChirpArgumentError("channelId required for \(options.channelType) send"))
            }
        }
        if content.hasPrefix("/"), let reason = routeCommand(content, senderId: selfId()) {
            return Promise.failed(RequestError(.blocked, message: reason))
        }
        var request = buildSendRequest(options: options, content: content)
        if let interceptor = interceptor {
            // try? collapses both "hook returned nil" and "hook threw" into
            // nil — both block the message (Kotlin catch → null).
            let effective = try? interceptor.onBeforeSend(request)
            guard let effective = effective else {
                return Promise.failed(
                    RequestError(.blocked, message: "message blocked by interceptor"))
            }
            request = effective
        }
        if let store = store {
            try? store.save(storedCopyOf(request))
        }
        return conn.request(spec: MsgSpecs.sendMessage, body: request).map { [weak self] response in
            if let self = self, let interceptor = self.interceptor {
                try? interceptor.onAfterSend(request)
            }
            return response
        }
    }

    /// nil = no command consumed the input, send it as a normal message.
    private func routeCommand(_ content: String, senderId: String) -> String? {
        let snapshot: [any CommandHandler] = withLock { commands }
        if snapshot.isEmpty { return nil }
        let body = String(content.dropFirst())
        let name: String
        let args: String
        if let space = body.firstIndex(of: " ") {
            name = String(body[..<space])
            args = String(body[body.index(after: space)...])
        } else {
            name = body
            args = ""
        }
        for command in snapshot where command.name == name {
            // A throwing handler declines (Kotlin runCatching → false).
            let handled = (try? command.execute(args: args, senderId: senderId)) ?? false
            if handled { return "command handled locally" }
        }
        return "unknown command, dropped locally: \(content)"
    }

    private func buildSendRequest(
        options: SendOptions,
        content: String
    ) -> Chirp_Chat_SendMessageRequest {
        var request = Chirp_Chat_SendMessageRequest()
        request.senderID = selfId()
        request.receiverID = options.receiverId ?? ""
        request.channelType = options.channelType
        switch options.channelType {
        case .`private`:
            // 1:1 channel id is the sorted user-id pair — both sides land in
            // the same bucket.
            request.channelID = [request.senderID, options.receiverId ?? ""]
                .sorted()
                .joined(separator: "|")
        default:
            request.channelID = options.channelId ?? ""
        }
        request.msgType = options.msgType
        request.content = Data(content.utf8)
        request.clientTimestamp = Int64(Date().timeIntervalSince1970 * 1000)
        request.replyToMessageID = options.replyToMessageId
        return request
    }

    /// The archive copy of an outgoing message: empty messageID is the
    /// sent-direction marker, timestamp = client timestamp.
    private func storedCopyOf(_ request: Chirp_Chat_SendMessageRequest) -> Chirp_Chat_ChatMessage {
        var message = Chirp_Chat_ChatMessage()
        message.senderID = request.senderID
        message.receiverID = request.receiverID
        message.channelType = request.channelType
        message.channelID = request.channelID
        message.msgType = request.msgType
        message.content = request.content
        message.timestamp = request.clientTimestamp
        message.replyToMessageID = request.replyToMessageID
        return message
    }

    // ---- incoming path -------------------------------------------------------

    private func onIncoming(_ body: [UInt8]) {
        let message: Chirp_Chat_ChatMessage
        do {
            message = try Chirp_Chat_ChatMessage(serializedBytes: Data(body))
        } catch {
            return // undecodable push: ignore, the link stays healthy
        }
        if let interceptor = interceptor {
            guard let effective = try? interceptor.onBeforeReceive(message) else {
                return // dropped by the receive interceptor
            }
            fanIn(effective)
        } else {
            fanIn(message)
        }
    }

    private func fanIn(_ message: Chirp_Chat_ChatMessage) {
        if let store = store {
            try? store.save(message) // archive failure must not kill the fan-out
        }
        for listener in snapshotListeners() {
            try? listener.onMessageReceived(message)
        }
        if let interceptor = interceptor {
            try? interceptor.onAfterReceive(message)
        }
    }

    private func onKickBody(_ body: [UInt8]) {
        // A body that is not a KickNotify still proves the session is dead;
        // it just carries an empty reason.
        let reason = (try? Chirp_Auth_KickNotify(serializedBytes: Data(body)))?.reason ?? ""
        for listener in snapshotListeners() {
            try? listener.onKicked(reason: reason)
        }
    }

    private func onDevicesPresenceBody(_ body: [UInt8]) {
        let devices: [Chirp_Auth_DevicePresence]
        do {
            devices = try Chirp_Auth_DevicesPresenceNotify(serializedBytes: Data(body)).devices
        } catch {
            return // malformed list: dropped
        }
        if devices.isEmpty { return }
        for listener in snapshotListeners() {
            try? listener.onDevicesPresence(devices)
        }
    }

    // ---- plumbing ------------------------------------------------------------

    private func withLock<T>(_ body: () -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body()
    }

    private func snapshotListeners() -> [any ChatEventListener] {
        withLock { listeners }
    }
}
