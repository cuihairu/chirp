import ChirpProtos
import Foundation
import SwiftProtobuf

/// Connection state machine (port of mobile_companion chirp_client.dart via
/// the Kotlin ChatConnection.kt, whose test vectors this package shares):
///   idle → connecting → connected ⇄ waitingReconnect (auto reconnect)
///                     ↘ kicked        (KICK_NOTIFY: terminal for auto
///                                      reconnect; an explicit connect() is a
///                                      fresh user action and restarts)
///   any  → closed        (manual disconnect(); terminal until connect())
public enum ConnState: Equatable, CustomStringConvertible {
    case idle
    case connecting
    case connected
    case waitingReconnect
    case closed
    case kicked

    public var description: String {
        switch self {
        case .idle: return "IDLE"
        case .connecting: return "CONNECTING"
        case .connected: return "CONNECTED"
        case .waitingReconnect: return "WAITING_RECONNECT"
        case .closed: return "CLOSED"
        case .kicked: return "KICKED"
        }
    }
}

/// Tunables, mirroring ChirpClientOptions defaults from the dart pipeline:
/// the server has no idle kick — 25s pings keep intermediaries honest; 2
/// consecutive pings without any pong mean the link is dead in practice.
public struct ChirpClientOptions {
    public var heartbeatIntervalMs: Int64
    public var maxMissedPongs: Int
    public var requestTimeoutMs: Int64
    public var reconnectBaseMs: Int64
    public var reconnectMaxMs: Int64
    /// ± fraction applied to each reconnect delay.
    public var jitterRatio: Double

    public init(
        heartbeatIntervalMs: Int64 = 25_000,
        maxMissedPongs: Int = 2,
        requestTimeoutMs: Int64 = 10_000,
        reconnectBaseMs: Int64 = 500,
        reconnectMaxMs: Int64 = 15_000,
        jitterRatio: Double = 0.2
    ) {
        self.heartbeatIntervalMs = heartbeatIntervalMs
        self.maxMissedPongs = maxMissedPongs
        self.requestTimeoutMs = requestTimeoutMs
        self.reconnectBaseMs = reconnectBaseMs
        self.reconnectMaxMs = reconnectMaxMs
        self.jitterRatio = jitterRatio
    }
}

/// Jitter seam for the reconnect backoff (dart Random; tests inject a
/// midpoint fake so the ladder is assertable).
public protocol RandomSource: AnyObject {
    /// Uniform in `0..<until`; `until <= 0` yields 0.
    func nextLong(until bound: Int64) -> Int64
}

public final class SystemRandomSource: RandomSource {
    public init() {}
    public func nextLong(until bound: Int64) -> Int64 {
        guard bound > 0 else { return 0 }
        return Int64.random(in: 0..<bound)
    }
}

private final class PendingRequest {
    let raw: Promise<[UInt8]>
    let timeout: Cancellable
    init(raw: Promise<[UInt8]>, timeout: Cancellable) {
        self.raw = raw
        self.timeout = timeout
    }
}

/// One live listener registration: identity token for unsubscribe (Swift
/// closures cannot be compared for equality, so removal goes by token).
private final class ListenerToken {
    let id: Int
    init(id: Int) { self.id = id }
}

/// Typed websocket client for the Packet protocol: u32 framing, request/
/// response correlation by sequence, request deadlines, heartbeats with
/// missed-pong detection, auto reconnect with jittered exponential backoff
/// and terminal kick handling. Faithful port of the dart ChirpClient; the
/// typed `MessageSpec` round-trip API is the Swift-shaped surface over
/// dart's decode-at-call-site request.
///
/// Dart runs everything on one event loop; here all state transitions hold
/// the recursive lock (transport callbacks arrive on URLSession/Darwin
/// threads or the scheduler queue, timers on the scheduler). `Scheduler` and
/// `RandomSource` are injectable so tests drive time and jitter
/// deterministically. Re-entrancy constraint (mirrors Kotlin/C++): handlers
/// registered here run under the lock — they must not call back into the
/// connection synchronously.
public final class ChatConnection {
    public let url: String
    public let options: ChirpClientOptions
    private let transportFactory: (String) -> WsTransport
    private let random: RandomSource
    private let scheduler: Scheduler

    /// Java-monitor semantics (close() inside pingTick re-enters via the
    /// synchronous fake/URLSession close path): recursive, one per connection.
    private let lock = NSRecursiveLock()

    private let decoder = FrameDecoder()
    private var seqCounter: Int64 = 0
    private var pending: [Int64: PendingRequest] = [:]
    private var notifyListeners: [Chirp_Gateway_MsgID: [(ListenerToken, ([UInt8]) -> Void)]] = [:]
    private var stateListeners: [(ListenerToken, (ConnState) -> Void)] = []
    private var reconnectListeners: [(ListenerToken, (Int, Int64) -> Void)] = []
    private var reconnectedListeners: [(ListenerToken, () -> Void)] = []
    private var listenerIds = 0

    public private(set) var state: ConnState = .idle

    /// True after the server sent KICK_NOTIFY; auto reconnect stays off.
    public private(set) var kicked = false

    /// server_time − local_time at the last pong; nil before the first.
    public private(set) var clockOffsetMs: Int64?

    private var ws: WsTransport?
    private var attempt = 0
    private var missedPongs = 0
    private var reconnectCancellable: Cancellable?
    private var heartbeatCancellable: Cancellable?

    public init(
        url: String,
        transportFactory: @escaping (String) -> WsTransport,
        options: ChirpClientOptions = ChirpClientOptions(),
        random: RandomSource = SystemRandomSource(),
        scheduler: Scheduler = DispatchScheduler()
    ) {
        self.url = url
        self.transportFactory = transportFactory
        self.options = options
        self.random = random
        self.scheduler = scheduler
    }

    /// Opens the socket. Completes when open, errors if it closes/fails
    /// first. An explicit connect() is a user action: it clears kick state
    /// and cancels any pending reconnect timer.
    public func connect() -> Promise<Void> {
        let result = Promise<Void>()
        let t: WsTransport = withLock {
            kicked = false
            cancelReconnectTimer()
            let stale = ws
            ws = nil
            stale?.close()
            setStatus(.connecting)
            let fresh = transportFactory(url)
            ws = fresh
            fresh.onBinary { [weak self] bytes in self?.handleData(fresh, bytes) }
            fresh.onClosed { [weak self] in self?.handleTransportClosed(fresh) }
            return fresh
        }
        t.open { [weak self] err in
            guard let self = self else { return }
            var superseded = false
            self.withLock {
                if self.ws !== t {
                    // disconnect() or a newer connect() raced the open: this
                    // attempt is superseded, so just settle its future.
                    superseded = true
                } else if let err = err {
                    result.completeError(err)
                } else {
                    self.missedPongs = 0
                    self.setStatus(.connected)
                    self.startHeartbeat()
                    // Observational only: a backoff counter > 0 means this
                    // open is a reconnect. resetBackoff() still owns the
                    // counter reset.
                    if self.attempt > 0 {
                        for (_, listener) in self.reconnectedListeners { listener() }
                    }
                    result.complete(())
                }
            }
            if superseded {
                result.completeError(
                    RequestError(.closed, message: "connect superseded"))
            } else if err != nil {
                self.handleTransportClosed(t)
            }
        }
        return result
    }

    /// Manual close; no reconnect follows.
    public func disconnect() {
        withLock {
            setStatus(.closed)
            clearHeartbeat()
            rejectAllPending(.closed)
            cancelReconnectTimer()
            ws?.close()
            ws = nil
        }
    }

    /// Reset the reconnect backoff after a successful login. Opening the
    /// socket alone proves little (the server may still refuse the token),
    /// so the api layer calls this once the LOGIN round-trip succeeded.
    public func resetBackoff() {
        withLock { attempt = 0 }
    }

    /// Immediate heartbeat (mobile app lifecycle resume).
    public func heartbeatNow() {
        withLock {
            if state == .connected { pingTick() }
        }
    }

    /// Typed request/response. Fails with `RequestError.Kind.closed` when
    /// not connected, `.timeout` on deadline; server error codes are the
    /// caller's business (the decoded response completes normally).
    public func request<T: SwiftProtobuf.Message>(
        spec: MessageSpec<T>,
        body: SwiftProtobuf.Message,
        timeoutMs: Int64? = nil
    ) -> Promise<T> {
        let raw = Promise<[UInt8]>()
        let deadline = timeoutMs ?? options.requestTimeoutMs
        var failed: Error?
        withLock {
            if state != .connected || ws == nil {
                failed = RequestError(.closed)
            } else {
                seqCounter += 1
                let seq = seqCounter
                let timeoutHandle = scheduler.post(delayMs: deadline) { [weak self] in
                    guard let self = self else { return }
                    var timedOut = false
                    self.withLock {
                        if let entry = self.pending.removeValue(forKey: seq) {
                            timedOut = true
                            entry.timeout.cancel()
                        }
                    }
                    if timedOut {
                        raw.completeError(RequestError(.timeout))
                    }
                }
                pending[seq] = PendingRequest(raw: raw, timeout: timeoutHandle)
                do {
                    try sendRaw(spec.reqMsgId, body: body.serializedBytes(), seq: seq)
                } catch {
                    timeoutHandle.cancel()
                    pending.removeValue(forKey: seq)
                    raw.completeError(
                        error is RequestError ? error : RequestError(.closed))
                }
            }
        }
        if let failed = failed {
            return failedPromise(failed)
        }
        let out = Promise<T>()
        raw.onComplete { outcome in
            switch outcome {
            case .success(let bytes):
                do {
                    out.complete(try spec.decodeResponse(bytes))
                } catch {
                    out.completeError(error)
                }
            case .failure(let error):
                out.completeError(error)
            }
        }
        return out
    }

    /// Fire-and-forget send for messages that never get a response frame
    /// (MESSAGE_ACK 2209, TYPING 2208). Throws `RequestError` closed when
    /// not connected.
    public func send(msgId: Chirp_Gateway_MsgID, body: [UInt8]) throws {
        try withLock {
            if state != .connected || ws == nil {
                throw RequestError(.closed)
            }
            seqCounter += 1
            try sendRaw(msgId, body: body, seq: seqCounter)
        }
    }

    /// Subscribe to server pushes (sequence === 0 packets). Returns
    /// unsubscribe.
    public func onNotify(
        msgId: Chirp_Gateway_MsgID,
        handler: @escaping ([UInt8]) -> Void
    ) -> () -> Void {
        var token: ListenerToken!
        withLock {
            listenerIds += 1
            token = ListenerToken(id: listenerIds)
            notifyListeners[msgId, default: []].append((token, handler))
        }
        return { [weak self] in self?.removeNotify(token, msgId: msgId) }
    }

    /// Subscribe to status flips. Returns unsubscribe.
    public func onStateChange(handler: @escaping (ConnState) -> Void) -> () -> Void {
        var token: ListenerToken!
        withLock {
            listenerIds += 1
            token = ListenerToken(id: listenerIds)
            stateListeners.append((token, handler))
        }
        return { [weak self] in self?.removeState(token) }
    }

    /// A backoff reconnect is about to fire: 1-based attempt and the delay it
    /// was scheduled with (jitter included). Only fires after the first drop.
    /// Returns unsubscribe.
    public func onReconnecting(
        listener: @escaping (Int, Int64) -> Void
    ) -> () -> Void {
        var token: ListenerToken!
        withLock {
            listenerIds += 1
            token = ListenerToken(id: listenerIds)
            reconnectListeners.append((token, listener))
        }
        return { [weak self] in self?.removeReconnecting(token) }
    }

    /// A reconnect attempt reached 'connected' again. The backoff counter is
    /// only reset by `resetBackoff` (login success) — this event is purely
    /// observational, so the retry cadence is unchanged. Returns unsubscribe.
    public func onReconnected(listener: @escaping () -> Void) -> () -> Void {
        var token: ListenerToken!
        withLock {
            listenerIds += 1
            token = ListenerToken(id: listenerIds)
            reconnectedListeners.append((token, listener))
        }
        return { [weak self] in self?.removeReconnected(token) }
    }

    // ---- internals: all take the lock (dart's event-loop serialization) ----

    private func withLock<T>(_ body: () throws -> T) rethrows -> T {
        lock.lock()
        defer { lock.unlock() }
        return try body()
    }

    private func removeNotify(_ token: ListenerToken, msgId: Chirp_Gateway_MsgID) {
        withLock {
            notifyListeners[msgId]?.removeAll { $0.0.id == token.id }
        }
    }

    private func removeState(_ token: ListenerToken) {
        withLock { stateListeners.removeAll { $0.0.id == token.id } }
    }

    private func removeReconnecting(_ token: ListenerToken) {
        withLock { reconnectListeners.removeAll { $0.0.id == token.id } }
    }

    private func removeReconnected(_ token: ListenerToken) {
        withLock { reconnectedListeners.removeAll { $0.0.id == token.id } }
    }

    private func setStatus(_ status: ConnState) {
        // Caller holds the lock.
        if state == status { return }
        state = status
        for (_, listener) in stateListeners { listener(status) }
    }

    private func startHeartbeat() {
        // Caller holds the lock.
        clearHeartbeat()
        heartbeatCancellable = scheduler.postPeriodic(intervalMs: options.heartbeatIntervalMs) {
            [weak self] in self?.pingTick()
        }
    }

    private func clearHeartbeat() {
        // Caller holds the lock.
        heartbeatCancellable?.cancel()
        heartbeatCancellable = nil
    }

    private func pingTick() {
        withLock {
            if state != .connected { return }
            if missedPongs >= options.maxMissedPongs {
                // Consecutive missed pongs: the link is dead in practice —
                // close it and let the reconnect path take over.
                ws?.close()
                return
            }
            missedPongs += 1
            var ping = Chirp_Gateway_HeartbeatPing()
            ping.timestamp = scheduler.nowMs()
            do {
                try sendRaw(.heartbeatPing, body: [UInt8](ping.serializedData()))
            } catch {
                // Socket died underneath us; the closed event will fire.
            }
        }
    }

    private func sendRaw(
        _ msgId: Chirp_Gateway_MsgID,
        body: [UInt8],
        seq: Int64? = nil
    ) throws {
        // Caller holds the lock.
        guard let t = ws else { throw RequestError(.closed) }
        var packet = Chirp_Gateway_Packet()
        packet.msgID = msgId
        packet.sequence = seq ?? nextSeq()
        packet.body = Data(body)
        let frame = try encodeFrame(payload: [UInt8](packet.serializedData()))
        if !t.send(frame) {
            throw RequestError(.closed)
        }
    }

    /// Caller holds the lock.
    private func nextSeq() -> Int64 {
        seqCounter += 1
        return seqCounter
    }

    private func handleData(_ t: WsTransport, _ bytes: [UInt8]) {
        withLock {
            if ws !== t { return } // stale transport from a superseded attempt
            let frames: [[UInt8]]
            do {
                frames = try decoder.feed(bytes)
            } catch {
                // A corrupted stream cannot be resynchronized; treat it as a
                // dead link (the close event drives the reconnect path).
                ws?.close()
                return
            }
            for frame in frames { handleFrame(frame) }
        }
    }

    private func handleFrame(_ frame: [UInt8]) {
        // Caller holds the lock.
        let packet: Chirp_Gateway_Packet
        do {
            packet = try Chirp_Gateway_Packet(serializedBytes: Data(frame))
        } catch {
            return // undecodable packet: ignore rather than kill the connection
        }
        if packet.sequence == 0 {
            dispatchNotify(packet)
            return
        }
        if packet.msgID == .heartbeatPong {
            // Pong echoes the ping's sequence and is not in the pending table.
            onPong(packet.body)
            return
        }
        guard let entry = pending.removeValue(forKey: packet.sequence) else {
            return // response to an already timed-out request
        }
        entry.timeout.cancel()
        entry.raw.complete([UInt8](packet.body))
    }

    private func onPong(_ body: Data) {
        missedPongs = 0
        if let pong = try? Chirp_Gateway_HeartbeatPong(serializedBytes: body) {
            clockOffsetMs = pong.serverTime - scheduler.nowMs()
        }
        // A malformed pong still proves the link is alive.
    }

    private func dispatchNotify(_ packet: Chirp_Gateway_Packet) {
        // Caller holds the lock.
        if packet.msgID == .kickNotify {
            markKicked()
            // Fall through: subscribers may also want the KickNotify reason body.
        }
        if let listeners = notifyListeners[packet.msgID] {
            for (_, handler) in listeners {
                handler([UInt8](packet.body))
                // One bad handler can only crash the process in Swift, not
                // starve the others — the dart/Kotlin try-per-handler shape
                // maps to Swift's no-throw convention here.
            }
        }
    }

    private func markKicked() {
        // Caller holds the lock.
        // KICK then reconnect would just fight the new device, so auto
        // reconnect stays off and the user is sent back to login. The state
        // flip happens HERE, not via the close event.
        kicked = true
        clearHeartbeat()
        rejectAllPending(.kicked)
        cancelReconnectTimer()
        setStatus(.kicked)
        ws?.close()
        ws = nil
    }

    private func handleTransportClosed(_ t: WsTransport) {
        withLock {
            if ws !== t { return } // stale transport (superseded attempt or manual path)
            ws = nil
            clearHeartbeat()
            rejectAllPending(.closed)
            decoder.reset()
            if kicked {
                setStatus(.kicked)
                return
            }
            if state == .closed { return } // manual disconnect()
            setStatus(.waitingReconnect)
            scheduleReconnect()
        }
    }

    private func scheduleReconnect() {
        // Caller holds the lock. base = min(base·2^attempt, max) — iterated
        // doubling with an in-loop clamp reaches the same value without
        // overflow for any reachable attempt count.
        var base = options.reconnectBaseMs
        for _ in 0..<min(attempt, 60) {
            if base < options.reconnectMaxMs {
                base = min(base << 1, options.reconnectMaxMs)
            }
        }
        let jitter = Int64((Double(base) * options.jitterRatio).rounded())
        let delay = base + random.nextLong(until: jitter * 2 + 1) - jitter
        attempt += 1
        for (_, listener) in reconnectListeners {
            listener(attempt, delay)
            // Listener errors cannot break the reconnect chain: Swift
            // handlers are non-throwing by contract.
        }
        reconnectCancellable = scheduler.post(delayMs: delay) { [weak self] in
            guard let self = self else { return }
            self.withLock { self.reconnectCancellable = nil }
            // A failed attempt loops straight back through handleTransportClosed.
            _ = self.connect()
        }
    }

    private func cancelReconnectTimer() {
        // Caller holds the lock.
        reconnectCancellable?.cancel()
        reconnectCancellable = nil
    }

    private func rejectAllPending(_ kind: RequestError.Kind) {
        // Caller holds the lock.
        for entry in pending.values {
            entry.timeout.cancel()
            entry.raw.completeError(RequestError(kind))
        }
        pending.removeAll()
    }

    private func failedPromise<T>(_ error: Error) -> Promise<T> {
        let p = Promise<T>()
        p.completeError(error)
        return p
    }
}
