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

    // ---- combinators (the CompletableFuture mapping surface) ----------------

    /// Kotlin thenApply: transform the value; an error skips the transform.
    public func map<U>(_ transform: @escaping (T) -> U) -> Promise<U> {
        let out = Promise<U>()
        onComplete { outcome in
            switch outcome {
            case .success(let value): out.complete(transform(value))
            case .failure(let error): out.completeError(error)
            }
        }
        return out
    }

    /// Kotlin thenCompose: chain onto another future.
    public func flatMap<U>(_ transform: @escaping (T) -> Promise<U>) -> Promise<U> {
        let out = Promise<U>()
        onComplete { outcome in
            switch outcome {
            case .success(let value):
                transform(value).onComplete { nested in
                    switch nested {
                    case .success(let v): out.complete(v)
                    case .failure(let error): out.completeError(error)
                    }
                }
            case .failure(let error): out.completeError(error)
            }
        }
        return out
    }

    /// Kotlin handle: see both outcomes as optionals; the handler's return
    /// value always completes the result (errors are consumed).
    public func handle<U>(_ handler: @escaping (T?, Error?) -> U) -> Promise<U> {
        let out = Promise<U>()
        onComplete { outcome in
            switch outcome {
            case .success(let value): out.complete(handler(value, nil))
            case .failure(let error): out.complete(handler(nil, error))
            }
        }
        return out
    }

    public static func completed(_ value: T) -> Promise<T> {
        let p = Promise<T>()
        p.complete(value)
        return p
    }

    public static func failed(_ error: Error) -> Promise<T> {
        let p = Promise<T>()
        p.completeError(error)
        return p
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
