import XCTest
@testable import ChirpAppCore

/// 群名单镜像(P4e):整表换入、本地移除、缺名回落。
final class GroupIndexTests: XCTestCase {
    func testReplaceSortsByIdAndSkipsEmptyIds() {
        let groups = GroupIndex()
        groups.replace([
            GroupIndex.Entry(info: groupInfo("g2", name: "二号", owner: "alice")),
            GroupIndex.Entry(info: groupInfo("g1", name: "一号", owner: "bob", members: 5)),
            GroupIndex.Entry(info: groupInfo("", name: "坏行", owner: "x")),
        ])
        let entries = groups.entries()
        XCTAssertEqual(entries.map(\.groupId), ["g1", "g2"], "groupId 字典序,空 id 跳过")
        XCTAssertEqual(entries[0].name, "一号")
        XCTAssertEqual(entries[0].ownerId, "bob")
        XCTAssertEqual(entries[0].memberCount, 5)
        XCTAssertEqual(entries[1].memberCount, 2, "缺省 members=2(构造默认)")
    }

    func testEntryFromProtoMapsAllFields() {
        let entry = GroupIndex.Entry(info: groupInfo("g9", name: "研发群", owner: "carol", members: 12))
        XCTAssertEqual(entry.groupId, "g9")
        XCTAssertEqual(entry.name, "研发群")
        XCTAssertEqual(entry.ownerId, "carol")
        XCTAssertEqual(entry.memberCount, 12)
    }

    func testRemoveAndUnknownRemoval() {
        let groups = GroupIndex()
        groups.replace([GroupIndex.Entry(info: groupInfo("g1", name: "一号", owner: "alice"))])
        groups.remove(groupId: "g1")
        groups.remove(groupId: "ghost")  // 未知 id 无副作用
        XCTAssertTrue(groups.entries().isEmpty)
        XCTAssertNil(groups.entry(groupId: "g1"))
    }

    func testNameFallsBackToGroupIdWhenMissingOrBlank() {
        let groups = GroupIndex()
        groups.replace([
            GroupIndex.Entry(info: groupInfo("g1", name: "一号", owner: "alice")),
            GroupIndex.Entry(info: groupInfo("g2", name: "", owner: "alice")),
        ])
        XCTAssertEqual(groups.name(groupId: "g1"), "一号")
        XCTAssertEqual(groups.name(groupId: "g2"), "g2", "空名回落群 id(web 同款)")
        XCTAssertEqual(groups.name(groupId: "ghost"), "ghost", "不在名单回落群 id")
    }

    func testResetDropsEverything() {
        let groups = GroupIndex()
        groups.replace([GroupIndex.Entry(info: groupInfo("g1", name: "一号", owner: "alice"))])
        groups.reset()
        XCTAssertTrue(groups.entries().isEmpty)
    }
}
