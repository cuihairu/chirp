import Foundation

/// Minimal CompletableFuture-shaped future for the request/response path:
/// settle once (value or error), register completion callbacks (fired
/// immediately when already settled, on the settling thread — mirroring
/// Kotlin thenApply semantics, which may run under the connection lock), and
/// a blocking `get` for tests and legacy call sites.
public final class Promise<T> {
    public enum Outcome {
        case success(T)
        case failure(Error)
    }

    private let mutex = NSLock()
    private var outcome: Outcome?
    private var callbacks: [(Outcome) -> Void] = []
    private let settled = DispatchSemaphore(value: 0)

    public init() {}

    public func complete(_ value: T) {
        settle(.success(value))
    }

    public func completeError(_ error: Error) {
        settle(.failure(error))
    }

    private func settle(_ o: Outcome) {
        mutex.lock()
        if outcome != nil {
            mutex.unlock()
            return
        }
        outcome = o
        let toFire = callbacks
        callbacks = []
        mutex.unlock()
        for cb in toFire { cb(o) }
        settled.signal()
    }

    /// Register a completion callback. Fires on the settling thread if the
    /// promise is already settled — callers that re-enter state under the
    /// connection lock must respect the pipeline re-entrancy rules.
    public func onComplete(_ cb: @escaping (Outcome) -> Void) {
        mutex.lock()
        if let o = outcome {
            mutex.unlock()
            cb(o)
            return
        }
        callbacks.append(cb)
        mutex.unlock()
    }

    /// Block until settled or the timeout elapses (which throws `.timeout`;
    /// a settled promise returns regardless of the deadline).
    @discardableResult
    public func get(timeoutSeconds: Double = 5) throws -> T {
        let waitResult = settled.wait(timeout: .now() + timeoutSeconds)
        mutex.lock()
        let o = outcome
        mutex.unlock()
        switch (waitResult, o) {
        case (.success, .some(.success(let value))):
            return value
        case (.success, .some(.failure(let error))):
            throw error
        default:
            throw RequestError(.timeout, message: "promise get() timed out")
        }
    }
}
