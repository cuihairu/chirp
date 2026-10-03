import XCTest
@testable import ChirpAppCore

/// P4e 起按键寻址('p:'+排序对 / 'g:'+群 id,web conversation key 同位)。
final class SessionIndexTests: XCTestCase {
    func testDmChannelIdJoinIsOrderIndependent() {
        XCTAssertEqual(SessionIndex.dmChannelId("alice", "bob"), "alice|bob")
        XCTAssertEqual(SessionIndex.dmChannelId("bob", "alice"), "alice|bob")
    }

    func testIncomingAggregatesPerPeerAndCountsUnread() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "你好", ts: 100))
        index.recordIncoming(dmText("bob", "alice", id: "m2", text: "在吗", ts: 200))
        index.recordIncoming(dmText("carol", "alice", id: "m3", text: "hi", ts: 150))

        // 入站 DM 折成排序对键——与出站 'p:alice|bob' 同桶。
        let summaries = index.summaries()
        XCTAssertEqual(summaries.map(\.key), ["p:alice|bob", "p:alice|carol"])  // 时间戳降序
        XCTAssertEqual(summaries[0].kind, .dm)
        XCTAssertEqual(summaries[0].peerId, "bob")
        XCTAssertEqual(summaries[0].lastMessage, "在吗")
        XCTAssertEqual(summaries[0].unread, 2)
        XCTAssertEqual(index.unreadTotal(), 3)
    }

    func testOutgoingUpdatesPreviewButNotUnread() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 100))
        index.recordOutgoing(
            key: "p:alice|bob", peerId: "bob", content: "在的", timestamp: 200)

        let summary = index.summary(key: "p:alice|bob")
        XCTAssertEqual(summary?.lastMessage, "在的")
        XCTAssertEqual(summary?.unread, 1, "发出侧不消耗未读")
    }

    func testMarkReadClearsUnreadOnly() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 100))
        index.markRead(key: "p:alice|bob")
        XCTAssertEqual(index.summary(key: "p:alice|bob")?.unread, 0)
        XCTAssertEqual(index.summary(key: "p:alice|bob")?.lastMessage, "在吗", "预览保留")
        index.markRead(key: "p:alice|nobody")  // 未知对端:无副作用
        XCTAssertNil(index.summary(key: "p:alice|nobody"))
    }

    func testEmptySenderDropped() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(dmText("", "alice", id: "m1", text: "?", ts: 100))
        XCTAssertTrue(index.summaries().isEmpty)
    }

    // ---- 群桶(P4e) -----------------------------------------------------------

    /// 群消息按频道归 'g:' 桶:不同发信人同一群落同一行,且**不再**按发信人
    /// 折出 DM 会话(回归向量:对端是群,不是发信人)。
    func testGroupIncomingBucketsByChannelNotSender() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(groupText("bob", groupId: "g1", id: "m1", text: "早", ts: 100))
        index.recordIncoming(groupText("carol", groupId: "g1", id: "m2", text: "好", ts: 200))

        let summaries = index.summaries()
        XCTAssertEqual(summaries.count, 1, "一个群一行,不按发信人裂行")
        XCTAssertEqual(summaries[0].key, "g:g1")
        XCTAssertEqual(summaries[0].kind, .group)
        XCTAssertEqual(summaries[0].peerId, "g1")
        XCTAssertEqual(summaries[0].lastMessage, "好")
        XCTAssertEqual(summaries[0].unread, 2)
        XCTAssertNil(
            index.summary(key: "p:alice|bob"), "群消息不落发信人的 DM 会话")
        XCTAssertNil(
            index.summary(key: "p:alice|carol"), "群消息不落发信人的 DM 会话")
    }

    func testGroupEmptyChannelDropped() {
        let index = SessionIndex(selfId: "alice")
        index.recordIncoming(groupText("bob", groupId: "", id: "m1", text: "?", ts: 100))
        XCTAssertTrue(index.summaries().isEmpty)
    }

    /// 名单引导:ensureGroup 先落空行(无预览),后续消息在同一行累计;
    /// remove 整行丢弃(预览与未读一并)。
    func testEnsureGroupBootstrapsRowThenRemoveDropsIt() {
        let index = SessionIndex(selfId: "alice")
        index.ensureGroup(groupId: "g1")
        var summary = index.summary(key: "g:g1")
        XCTAssertEqual(summary?.kind, .group)
        XCTAssertEqual(summary?.lastMessage, "")
        XCTAssertEqual(summary?.unread, 0)

        index.recordIncoming(groupText("bob", groupId: "g1", id: "m1", text: "早", ts: 100))
        summary = index.summary(key: "g:g1")
        XCTAssertEqual(summary?.lastMessage, "早")
        XCTAssertEqual(summary?.unread, 1)

        index.ensureGroup(groupId: "g1")  // 幂等:已有条目不动预览
        XCTAssertEqual(index.summary(key: "g:g1")?.unread, 1)
        index.ensureGroup(groupId: "")  // 空 id 无副作用

        index.remove(key: "g:g1")
        XCTAssertNil(index.summary(key: "g:g1"))
        XCTAssertEqual(index.unreadTotal(), 0)
    }
}
