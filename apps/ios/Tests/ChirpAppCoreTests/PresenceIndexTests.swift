import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 对端在线状态数据面(P4c):按 user_id 键控、70s TTL 兜底、空 id 丢弃、reset。
final class PresenceIndexTests: XCTestCase {
    private var index: PresenceIndex!

    override func setUp() {
        super.setUp()
        index = PresenceIndex()
    }

    func testSetAndReadBack() {
        index.set(
            "u1", status: .inGame, statusMessage: "副本中", atMs: 1_000)
        let entry = index.entry("u1")
        XCTAssertEqual(entry?.userId, "u1")
        XCTAssertEqual(entry?.status, .inGame)
        XCTAssertEqual(entry?.statusMessage, "副本中")
        XCTAssertEqual(entry?.atMs, 1_000)
    }

    func testEmptyUserIdIsIgnored() {
        index.set("", status: .online, statusMessage: "", atMs: 1)
        XCTAssertNil(index.entry(""))
        XCTAssertFalse(index.isFresh("", nowMs: 1))
    }

    func testLaterSnapshotWins() {
        index.set("u1", status: .online, statusMessage: "", atMs: 1_000)
        index.set("u1", status: .dnd, statusMessage: "开会", atMs: 2_000)
        let entry = index.entry("u1")
        XCTAssertEqual(entry?.status, .dnd)
        XCTAssertEqual(entry?.atMs, 2_000)
    }

    func testFreshnessExpiresAtTtl() {
        index.set("u1", status: .online, statusMessage: "", atMs: 1_000)
        XCTAssertTrue(index.isFresh("u1", nowMs: 1_000))
        XCTAssertTrue(
            index.isFresh("u1", nowMs: 1_000 + PresenceIndex.ttlMs - 1))
        XCTAssertFalse(
            index.isFresh("u1", nowMs: 1_000 + PresenceIndex.ttlMs),
            "到 70s 即按离线渲染(漏 disconnect notify 的兜底)")
    }

    func testUnknownUserIsNeverFresh() {
        XCTAssertNil(index.entry("ghost"))
        XCTAssertFalse(index.isFresh("ghost", nowMs: 0))
    }

    func testRemoveDropsOnlyThatUser() {
        index.set("u1", status: .online, statusMessage: "", atMs: 1)
        index.set("u2", status: .away, statusMessage: "", atMs: 1)
        index.remove("u1")
        index.remove("u1")
        XCTAssertNil(index.entry("u1"))
        XCTAssertEqual(index.entry("u2")?.status, .away, "删好友不该清别人的快照")
    }

    func testResetDropsEverything() {
        index.set("u1", status: .online, statusMessage: "", atMs: 1)
        index.reset()
        XCTAssertNil(index.entry("u1"))
        XCTAssertFalse(index.isFresh("u1", nowMs: 1))
        index.set("u2", status: .away, statusMessage: "", atMs: 2)
        XCTAssertTrue(index.isFresh("u2", nowMs: 2))
    }
}