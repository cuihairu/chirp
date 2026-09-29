import Foundation
import ChirpProtocol

/// Deterministic virtual clock for tests: advance() runs due tasks in time
/// order on the calling thread; tasks posted during an advance with a due
/// time still inside the window fire too. Not thread-safe (test-only).
/// Port of the Kotlin ManualScheduler (same semantics, same vectors).
public final class ManualScheduler: Scheduler {
    private final class Task {
        let dueMs: Int64
        let seq: Int64
        let body: () -> Void
        init(dueMs: Int64, seq: Int64, body: @escaping () -> Void) {
            self.dueMs = dueMs
            self.seq = seq
            self.body = body
        }
    }

    private var queue: [Task] = []
    private var cancelled = Set<Int64>()
    private var clock: Int64
    private var ids: Int64 = 0
    private var running = false

    public init(startMs: Int64 = 0) {
        clock = startMs
    }

    public func nowMs() -> Int64 { clock }

    private func post0(delayMs: Int64, task: @escaping () -> Void) -> Cancellable {
        ids += 1
        let id = ids
        queue.append(Task(dueMs: clock + delayMs, seq: id, body: task))
        return FakeCancellable { [weak self] in self?.cancelled.insert(id) }
    }

    public func post(delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        post0(delayMs: delayMs, task: task)
    }

    public func postPeriodic(intervalMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        // One step queued at a time; the step re-queues itself when it fires,
        // so cancelling the returned handle stops the whole chain.
        return post0(delayMs: intervalMs) { [weak self] in
            self?.step(intervalMs: intervalMs, task: task)
        }
    }

    private func step(intervalMs: Int64, task: @escaping () -> Void) {
        task()
        _ = post0(delayMs: intervalMs) { [weak self] in
            self?.step(intervalMs: intervalMs, task: task)
        }
    }

    /// Advance the virtual clock, firing every due (and not cancelled) task.
    public func advance(_ ms: Int64) {
        precondition(!running, "advance() is not reentrant")
        running = true
        defer { running = false }
        let target = clock + ms
        while true {
            guard let earliest = queue.min(by: { ($0.dueMs, $0.seq) < ($1.dueMs, $1.seq) }) else { break }
            if earliest.dueMs > target { break }
            queue.removeAll { $0.seq == earliest.seq }
            if cancelled.contains(earliest.seq) { continue }
            clock = earliest.dueMs
            earliest.body()
        }
        clock = target
    }

    /// Queued, not-yet-cancelled task count (reconnect timers, heartbeats…).
    public func pendingTasks() -> Int {
        queue.filter { !cancelled.contains($0.seq) }.count
    }

    private final class FakeCancellable: Cancellable {
        private let body: () -> Void
        init(_ body: @escaping () -> Void) { self.body = body }
        func cancel() { body() }
    }
}

/// Random whose nextLong(bound) returns the bound's midpoint. The reconnect
/// delay is base + nextLong(2·jitter+1) − jitter, so the midpoint makes the
/// jitter cancel and the scheduled delay equal base exactly at every rung of
/// the ladder.
public final class FakeRandom: RandomSource {
    public init() {}
    public func nextLong(until bound: Int64) -> Int64 {
        bound > 0 ? (bound - 1) / 2 : 0
    }
}

/// Scripted WsTransport double: captures sends, pushes inbound messages.
/// Port of the Kotlin FakeWsTransport.
public final class FakeWsTransport: WsTransport {
    public private(set) var sent: [[UInt8]] = []
    public private(set) var closeCount = 0
    public private(set) var openAnnounced: ((Error?) -> Void)?
    public private(set) var binaryHandler: (([UInt8]) -> Void)?
    public private(set) var closedHandler: (() -> Void)?

    /// nil = open succeeds; anything else = open fails with it.
    public var failOpenWith: Error?

    /// false = open() only records the callback; the test announces later.
    public var autoOpen = true

    public init() {}

    public func open(_ onResult: @escaping (Error?) -> Void) {
        openAnnounced = onResult
        if autoOpen {
            onResult(failOpenWith)
        }
    }

    public func onBinary(_ handler: @escaping ([UInt8]) -> Void) {
        binaryHandler = handler
    }

    public func onClosed(_ handler: @escaping () -> Void) {
        closedHandler = handler
    }

    public func send(_ data: [UInt8]) -> Bool {
        sent.append(data)
        return true
    }

    public func close() {
        closeCount += 1
        // Mirrors the dart IoWebSocket: closing announces the down event
        // exactly once.
        closedHandler?()
    }

    /// Deliver one inbound WebSocket binary message (raw bytes).
    public func deliverWsMessage(_ bytes: [UInt8]) {
        binaryHandler!(bytes)
    }

    /// The last outgoing frame's inner payload (u32 length prefix stripped).
    public func lastSentPayload() -> [UInt8] {
        let frame = sent.last!
        let length = Int(frame[0]) << 24 | Int(frame[1]) << 16 | Int(frame[2]) << 8 | Int(frame[3])
        return Array(frame[4..<(4 + length)])
    }
}
