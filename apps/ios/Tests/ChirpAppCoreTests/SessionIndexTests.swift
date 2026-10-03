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

    // ---- 快照持久化(P6) ------------------------------------------------------

    /// 构造回灌:预览/未读/时间戳跨「重启」存活;kind/peerId 从键反解
    /// (线格式不存冗余)。
    func testSnapshotHydratesSummariesOnInit() {
        let mem = MemorySnapshotIO()
        let seed = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        seed.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 200))
        seed.recordOutgoing(
            key: "p:alice|carol", peerId: "carol", content: "hi", timestamp: 300)
        seed.ensureGroup(groupId: "g1")

        let restored = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        let summaries = restored.summaries()
        XCTAssertEqual(
            summaries.map(\.key), ["p:alice|carol", "p:alice|bob", "g:g1"],
            "时间戳降序,无预览的群行垫底")
        XCTAssertEqual(summaries[1].unread, 1, "未读跨启动存活")
        XCTAssertEqual(summaries[1].lastMessage, "在吗")
        XCTAssertEqual(summaries[2].kind, .group)
        XCTAssertEqual(summaries[2].peerId, "g1")
    }

    /// 线格式直投(v1):坏行丢(畸形键/未知前缀/别人的 DM 对),负未读钳 0
    /// ——键即身份,载入过 SessionChannel 结构门 + 含自己侧的成员门。
    func testSnapshotDropsMalformedForeignRowsAndClampsUnread() {
        let mem = MemorySnapshotIO()
        mem.data = Data("""
            {"v":1,"rows":[
              {"key":"p:a|b|c","lastMessage":"x","lastTimestamp":1,"unread":1},
              {"key":"x:y","lastMessage":"x","lastTimestamp":1,"unread":1},
              {"key":"p:carol|dave","lastMessage":"别家","lastTimestamp":2,"unread":5},
              {"key":"p:alice|bob","lastMessage":"合法","lastTimestamp":3,"unread":-4},
              {"key":"g:g1","lastMessage":"群","lastTimestamp":4,"unread":2}
            ]}
            """.utf8)
        let index = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        XCTAssertEqual(
            index.summaries().map(\.key), ["g:g1", "p:alice|bob"],
            "只剩合法且含自己侧的行")
        XCTAssertEqual(index.summary(key: "p:alice|bob")?.unread, 0, "负未读钳 0")
        XCTAssertEqual(index.summary(key: "g:g1")?.unread, 2)
    }

    /// 异版本与破损字节:一律当全新开始(预览是可重建的缓存面,不报错)。
    func testSnapshotVersionGateAndCorruptionYieldFreshIndex() {
        let mem = MemorySnapshotIO()
        mem.data = Data("""
            {"v":2,"rows":[{"key":"p:alice|bob","lastMessage":"x","lastTimestamp":1,"unread":1}]}
            """.utf8)
        XCTAssertTrue(
            SessionIndex(selfId: "alice", snapshotIO: mem.io()).summaries().isEmpty,
            "异版本不猜格式")

        mem.data = Data("not json".utf8)
        XCTAssertTrue(
            SessionIndex(selfId: "alice", snapshotIO: mem.io()).summaries().isEmpty,
            "破损字节当全新开始")
    }

    /// markRead/remove 的变更也落盘:清零与摘行都跨重启生效。
    func testMarkReadAndRemovePersistAcrossRelaunch() {
        let mem = MemorySnapshotIO()
        let seed = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        seed.recordIncoming(dmText("bob", "alice", id: "m1", text: "在吗", ts: 100))
        seed.markRead(key: "p:alice|bob")

        let restored = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        XCTAssertEqual(restored.summary(key: "p:alice|bob")?.unread, 0, "清零跨启动")
        XCTAssertEqual(
            restored.summary(key: "p:alice|bob")?.lastMessage, "在吗", "预览保留")

        restored.remove(key: "p:alice|bob")
        let afterDelete = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        XCTAssertTrue(afterDelete.summaries().isEmpty, "删除跨启动")
    }

    /// 空操作不落盘:未知键 markRead/remove、已有行 ensureGroup 都不写。
    func testNoOpMutationsDoNotSave() {
        let mem = MemorySnapshotIO()
        let index = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        XCTAssertEqual(mem.saves, 0, "构造不写(无快照/无变更)")

        index.markRead(key: "p:alice|nobody")
        index.remove(key: "g:ghost")
        XCTAssertEqual(mem.saves, 0, "未知键不动不写")

        index.ensureGroup(groupId: "g1")
        XCTAssertEqual(mem.saves, 1, "新行落一盘")
        index.ensureGroup(groupId: "g1")
        XCTAssertEqual(mem.saves, 1, "幂等不再写")
    }

    /// 名单引导不冲掉回灌的预览(登录后 refreshGroups 的路径)。
    func testEnsureGroupKeepsHydratedPreview() {
        let mem = MemorySnapshotIO()
        let seed = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        seed.ensureGroup(groupId: "g1")
        seed.recordIncoming(groupText("bob", groupId: "g1", id: "m1", text: "早", ts: 100))

        let restored = SessionIndex(selfId: "alice", snapshotIO: mem.io())
        restored.ensureGroup(groupId: "g1")
        let summary = restored.summary(key: "g:g1")
        XCTAssertEqual(summary?.lastMessage, "早", "已有行不动预览")
        XCTAssertEqual(summary?.unread, 1)
    }
}
