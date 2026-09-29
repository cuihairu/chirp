import ChirpProtos
import XCTest
@testable import ChirpProtocol

/// Same vector group as Kotlin OfflineSendQueueTest (9 tests).
final class OfflineSendQueueTests: XCTestCase {
    private let privatePeer = SendOptions(channelType: .`private`, receiverId: "alice")

    private func okResponse() -> Chirp_Chat_SendMessageResponse {
        var r = Chirp_Chat_SendMessageResponse()
        r.code = .ok
        return r
    }

    private func okFuture() -> Promise<Chirp_Chat_SendMessageResponse> {
        Promise<Chirp_Chat_SendMessageResponse>.completed(okResponse())
    }

    private func flushCount(_ queue: OfflineSendQueue) throws -> Int {
        try queue.flush().get()
    }

    func testEnqueueDedupesByClientIdAndEvictsOldestBeyondCap() {
        var now: Int64 = 1_000
        let queue = OfflineSendQueue(
            send: { _, _ in Promise<Chirp_Chat_SendMessageResponse>.completed(self.okResponse()) },
            clock: { now += 1; return now },
            maxQueued: 2
        )
        XCTAssertTrue(queue.enqueue(clientId: "c1", options: privatePeer, content: "one"))
        XCTAssertFalse(queue.enqueue(clientId: "c1", options: privatePeer, content: "one-dup"))
        XCTAssertTrue(queue.enqueue(clientId: "c2", options: privatePeer, content: "two"))
        XCTAssertTrue(queue.enqueue(clientId: "c3", options: privatePeer, content: "three")) // evicts c1

        XCTAssertEqual(["c2", "c3"], queue.pending().map(\.clientId))
        XCTAssertEqual("two", queue.pending().first?.content)
        XCTAssertEqual(1_002, queue.pending().first?.queuedAtMs)
    }

    func testFlushReplaysInOrderAndCountsConfirmedSends() throws {
        var sentContents: [String] = []
        let queue = OfflineSendQueue(
            send: { _, content in
                sentContents.append(content)
                return Promise<Chirp_Chat_SendMessageResponse>.completed(
                    self.okResponse())
            }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        queue.enqueue(clientId: "c2", options: privatePeer, content: "two")
        XCTAssertEqual(2, try flushCount(queue))
        XCTAssertEqual(["one", "two"], sentContents)
        XCTAssertEqual(0, queue.size())
    }

    func testFlushStopsAtStillOfflineAndKeepsTheTailQueued() throws {
        var calls: [String] = []
        let queue = OfflineSendQueue(
            send: { _, content in
                calls.append(content)
                return Promise<Chirp_Chat_SendMessageResponse>.failed(RequestError(.closed))
            }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        queue.enqueue(clientId: "c2", options: privatePeer, content: "two")
        XCTAssertEqual(0, try flushCount(queue))
        XCTAssertEqual(["one"], calls) // first CLOSED stops the batch
        XCTAssertEqual(["c1", "c2"], queue.pending().map(\.clientId))
    }

    func testLocallyRejectedEntriesAreDroppedNotRetried() throws {
        let queue = OfflineSendQueue(
            send: { _, _ in
                Promise<Chirp_Chat_SendMessageResponse>.failed(
                    RequestError(.blocked, message: "interceptor"))
            }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        XCTAssertEqual(0, try flushCount(queue))
        XCTAssertEqual(0, queue.size())
    }

    func testArgumentErrorsCountAsRejected() throws {
        let queue = OfflineSendQueue(
            send: { _, _ in
                Promise<Chirp_Chat_SendMessageResponse>.failed(
                    ChirpArgumentError("no receiver"))
            }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        _ = try queue.flush().get()
        XCTAssertEqual(0, queue.size())
    }

    func testTimeoutKeepsTheEntryForAtLeastOnceReplay() throws {
        let queue = OfflineSendQueue(
            send: { _, _ in Promise<Chirp_Chat_SendMessageResponse>.failed(RequestError(.timeout)) }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        XCTAssertEqual(0, try flushCount(queue))
        XCTAssertEqual(1, queue.size())
    }

    func testAnyServerResponseIncludingTargetOfflineConfirmsTheEntry() throws {
        let queue = OfflineSendQueue(
            send: { _, _ in
                var response = Chirp_Chat_SendMessageResponse()
                response.code = .targetOffline
                return Promise<Chirp_Chat_SendMessageResponse>.completed(response)
            }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        XCTAssertEqual(1, try flushCount(queue))
        XCTAssertEqual(0, queue.size())
    }

    func testEmptyFlushIsZeroAndDoesNotCallSend() throws {
        var calls = 0
        let queue = OfflineSendQueue(
            send: { _, _ in
                calls += 1
                return Promise<Chirp_Chat_SendMessageResponse>.completed(self.okResponse())
            }
        )
        XCTAssertEqual(0, try flushCount(queue))
        XCTAssertEqual(0, calls)
    }

    func testClearEmptiesTheQueue() {
        let queue = OfflineSendQueue(
            send: { _, _ in Promise<Chirp_Chat_SendMessageResponse>.completed(self.okResponse()) }
        )
        queue.enqueue(clientId: "c1", options: privatePeer, content: "one")
        queue.clear()
        XCTAssertEqual(0, queue.size())
    }
}
