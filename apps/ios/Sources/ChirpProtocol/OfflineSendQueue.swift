import ChirpProtos
import Foundation

/// At-least-once offline send queue (port of OfflineSendQueue.kt). Entries
/// replay in queue order when the link is back:
///   - any server response (even TARGET_OFFLINE) confirms and drops the entry;
///   - CLOSED/TIMEOUT keeps the entry AND stops the batch (the tail stays
///     queued in order for the next flush);
///   - BLOCKED / argument errors / anything else drops the entry (retrying
///     can never succeed);
///   - dedupe by clientId at enqueue; beyond maxQueued the oldest entry is
///     evicted.
/// The `send` closure returns `Promise<Chirp_Chat_SendMessageResponse>` — the
/// Swift mapping of the Kotlin `(SendOptions, String) ->
/// CompletableFuture<SendMessageResponse>` seam.
public final class OfflineSendQueue {
    public struct Entry {
        public let clientId: String
        public let options: SendOptions
        public let content: String
        public let queuedAtMs: Int64
    }

    public typealias Sender = (SendOptions, String) -> Promise<Chirp_Chat_SendMessageResponse>

    private let send: Sender
    private let clock: () -> Int64
    private let maxQueued: Int
    private let lock = NSLock()
    // Front = oldest.
    private var queue: [Entry] = []

    public init(
        send: @escaping Sender,
        clock: @escaping () -> Int64 = { Int64(Date().timeIntervalSince1970 * 1000) },
        maxQueued: Int = 50
    ) {
        self.send = send
        self.clock = clock
        self.maxQueued = maxQueued
    }

    /// false = a duplicate clientId, nothing enqueued.
    @discardableResult
    public func enqueue(clientId: String, options: SendOptions, content: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if queue.contains(where: { $0.clientId == clientId }) {
            return false
        }
        let entry = Entry(
            clientId: clientId, options: options, content: content, queuedAtMs: clock())
        if queue.count >= maxQueued {
            queue.removeFirst() // full: drop the oldest
        }
        queue.append(entry)
        return true
    }

    public func size() -> Int {
        lock.lock()
        defer { lock.unlock() }
        return queue.count
    }

    public func pending() -> [Entry] {
        lock.lock()
        defer { lock.unlock() }
        return queue
    }

    public func clear() {
        lock.lock()
        defer { lock.unlock() }
        queue.removeAll()
    }

    /// Replays queued entries sequentially. Returns the number of entries
    /// confirmed by a server response this flush.
    public func flush() -> Promise<Int> {
        lock.lock()
        let snapshot = queue
        lock.unlock()
        return snapshot.isEmpty ? .completed(0) : replay(batch: snapshot)
    }

    private func replay(batch: [Entry]) -> Promise<Int> {
        let out = Promise<Int>()
        var confirmed = 0
        var aborted = false
        // thenCompose chain, folded: each link only starts after the previous
        // entry settles (settle-once guarantees the ordering).
        var chain = Promise<Bool>.completed(true)
        for entry in batch {
            chain = chain.flatMap { [weak self] _ -> Promise<Bool> in
                guard let self = self, !aborted else {
                    return Promise<Bool>.completed(true)
                }
                return self.send(entry.options, entry.content).handle { [weak self] _, err in
                    guard let self = self else { return true }
                    if err == nil {
                        confirmed += 1
                        self.drop(clientId: entry.clientId)
                    } else if let kind = (err as? RequestError)?.kind,
                        kind == .closed || kind == .timeout
                    {
                        // Still offline: keep this entry and everything after
                        // it, stop the batch.
                        aborted = true
                    } else {
                        // Rejected locally (BLOCKED, argument error, anything
                        // else): retrying can never succeed — drop.
                        self.drop(clientId: entry.clientId)
                    }
                    return true
                }
            }
        }
        chain.onComplete { _ in
            out.complete(confirmed)
        }
        return out
    }

    private func drop(clientId: String) {
        lock.lock()
        defer { lock.unlock() }
        queue.removeAll { $0.clientId == clientId }
    }
}
