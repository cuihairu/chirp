import XCTest
@testable import ChirpAppCore

final class SessionIndexTests: XCTestCase {
    func testDmChannelIdJoinIsOrderIndependent() {
        XCTAssertEqual(SessionIndex.dmChannelId("alice", "bob"), "alice|bob")
        XCTAssertEqual(SessionIndex.dmChannelId("bob", "alice"), "alice|bob")
    }

    func testIncomingAggregatesPerPeerAndCountsUnread() {
        let index = SessionIndex()
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "你好", ts: 100))
        index.recordIncoming(dmText("bob", "alice", id: "m2", text: "在吗", ts: 200))
        index.recordIncoming(dmText("carol", "alice", id: "m3", text: "hi", ts: 150))

        let summaries = index.summaries()
        XCTAssertEqual(summaries.map(\.peerId), ["bob", "carol"])  // 时间戳降序
        XCTAssertEqual(summaries[0].lastMessage, "在吗")
        XCTAssertEqual(summaries[0].unread, 2)
        XCTAssertEqual(index.unreadTotal(), 3)
    }

    func testOutgoingUpdatesPreviewButNotUnread() {
        let index = SessionIndex()
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 100))
        index.recordOutgoing(peerId: "bob", content: "在的", timestamp: 200)

        let summary = index.summary(peerId: "bob")
        XCTAssertEqual(summary?.lastMessage, "在的")
        XCTAssertEqual(summary?.unread, 1, "发出侧不消耗未读")
    }

    func testMarkReadClearsUnreadOnly() {
        let index = SessionIndex()
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 100))
        index.markRead(peerId: "bob")
        XCTAssertEqual(index.summary(peerId: "bob")?.unread, 0)
        XCTAssertEqual(index.summary(peerId: "bob")?.lastMessage, "在吗", "预览保留")
        index.markRead(peerId: "nobody")  // 未知对端:无副作用
        XCTAssertNil(index.summary(peerId: "nobody"))
    }

    func testEmptySenderDropped() {
        let index = SessionIndex()
        index.recordIncoming(dmText("", "alice", id: "m1", text: "?", ts: 100))
        XCTAssertTrue(index.summaries().isEmpty)
    }
}
