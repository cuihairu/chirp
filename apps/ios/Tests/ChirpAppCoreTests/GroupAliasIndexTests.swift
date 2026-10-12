import XCTest
@testable import ChirpAppCore

/// 群昵称快照数据面:名册整群重建 / 2124 单点折叠 / 随群行清理。
final class GroupAliasIndexTests: XCTestCase {
    func testAliasOfDefaultsToEmptyWhenUnknown() {
        let index = GroupAliasIndex()
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "bob"), "")
    }

    func testReplaceRebuildsGroupSkippingEmptyAliases() {
        let index = GroupAliasIndex()
        index.replace(groupId: "g1", members: [
            ("alice", "群主大大"),
            ("bob", ""),        // 空别名 = 未设,不落快照
            ("carol", "小卡"),
        ])
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "alice"), "群主大大")
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "bob"), "")
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "carol"), "小卡")

        // 重建以名册为准:上轮有的别名,这轮没了即视为已清除。
        index.replace(groupId: "g1", members: [("alice", "")])
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "alice"), "")
    }

    func testFoldSetsClearsAndIgnoresUnknownGroup() {
        let index = GroupAliasIndex()
        // 名册未载入的群不落快照:2124 先到也只是忽略(web 同款)。
        index.fold(groupId: "g9", userId: "bob", alias: "九爷")
        XCTAssertEqual(index.aliasOf(groupId: "g9", userId: "bob"), "")

        index.replace(groupId: "g1", members: [("bob", "老包")])
        index.fold(groupId: "g1", userId: "bob", alias: "包大人")
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "bob"), "包大人")
        // 空串 = 清除:移出快照,回退本名。
        index.fold(groupId: "g1", userId: "bob", alias: "")
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "bob"), "")
    }

    func testRemoveAndResetClearSnapshots() {
        let index = GroupAliasIndex()
        index.replace(groupId: "g1", members: [("bob", "老包")])
        index.replace(groupId: "g2", members: [("carol", "小卡")])
        index.remove(groupId: "g1")
        XCTAssertEqual(index.aliasOf(groupId: "g1", userId: "bob"), "")
        XCTAssertEqual(index.aliasOf(groupId: "g2", userId: "carol"), "小卡")
        index.reset()
        XCTAssertEqual(index.aliasOf(groupId: "g2", userId: "carol"), "")
    }
}
