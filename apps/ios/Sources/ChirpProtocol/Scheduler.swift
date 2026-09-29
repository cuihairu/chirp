import Foundation

/// Handle for a scheduled task; cancel() before it fires drops it.
public protocol Cancellable: AnyObject {
    func cancel()
}

/// Time seam for heartbeats, request deadlines and reconnect backoff (the
/// dart pipeline leans on the event-loop Timer; tests inject a manual
/// scheduler and drive the clock by hand).
public protocol Scheduler: AnyObject {
    func nowMs() -> Int64

    /// Run `task` once after `delayMs`.
    @discardableResult
    func post(delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable

    /// Run `task` every `intervalMs` until cancelled (fixed-delay).
    @discardableResult
    func postPeriodic(intervalMs: Int64, _ task: @escaping () -> Void) -> Cancellable
}

/// Production scheduler on a shared serial dispatch queue (process-wide: one
/// ticker queue for every connection, ticks are cheap state guards).
/// Tests inject a manual scheduler instead.
public final class DispatchScheduler: Scheduler {
    private let queue = DispatchQueue(label: "chirp.protocol.scheduler")

    public init() {}

    public func nowMs() -> Int64 {
        Int64(Date().timeIntervalSince1970 * 1000)
    }

    public func post(delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .milliseconds(Int(delayMs)))
        timer.setEventHandler(handler: task)
        timer.resume()
        return DispatchCancellable { timer.cancel() }
    }

    public func postPeriodic(intervalMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        // Fixed-delay has no direct DispatchSource shape; the leeway-free
        // repeating timer is close enough for heartbeat cadence (the missed-
        // pong counter, not the wall clock, decides liveness).
        timer.schedule(deadline: .now() + .milliseconds(Int(intervalMs)),
                       repeating: .milliseconds(Int(intervalMs)))
        timer.setEventHandler(handler: task)
        timer.resume()
        return DispatchCancellable { timer.cancel() }
    }

    private final class DispatchCancellable: Cancellable {
        private let body: () -> Void
        private let once = NSLock()
        private var done = false
        init(_ body: @escaping () -> Void) { self.body = body }
        func cancel() {
            once.lock()
            if !done { done = true; body() }
            once.unlock()
        }
    }
}
