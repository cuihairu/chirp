import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 快捷反应数据面(P4a):增量事件记账、聚合覆盖、清零删槽、owner 语义。
final class ReactionIndexTests: XCTestCase {
    private var index: ReactionIndex!

    override func setUp() {
        super.setUp()
        index = ReactionIndex()
    }

    func testAddRecordsCountAndMine() {
        index.record(messageId: "m1", emoji: "👍", userId: "alice", added: true, selfId: "alice")
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: true, selfId: "alice")
        let tallies = index.tallies(messageId: "m1")
        XCTAssertEqual(tallies.count, 1)
        XCTAssertEqual(tallies[0].emoji, "👍")
        XCTAssertEqual(tallies[0].count, 2)
        XCTAssertTrue(tallies[0].isMine)
        XCTAssertTrue(index.isMine(messageId: "m1", emoji: "👍"))
    }

    func testRemoveDecrementsAndDropsEmptySlot() {
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: true, selfId: "alice")
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: false, selfId: "alice")
        XCTAssertTrue(index.tallies(messageId: "m1").isEmpty, "清零即删槽")
        XCTAssertFalse(index.isMine(messageId: "m1", emoji: "👍"))
    }

    func testRemoveClampsAtZeroAndSkipsEmptyKeys() {
        // 未知消息/未知槽的 remove 不产生负数,也不引入新槽。
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: false, selfId: "alice")
        XCTAssertTrue(index.tallies(messageId: "m1").isEmpty)
        // 空 id / 空 emoji 事件直接丢弃。
        index.record(messageId: "", emoji: "👍", userId: "bob", added: true, selfId: "alice")
        index.record(messageId: "m1", emoji: "", userId: "bob", added: true, selfId: "alice")
        XCTAssertTrue(index.tallies(messageId: "m1").isEmpty)
    }

    func testApplyAggregateOverridesSlot() {
        // 先到他人事件,再用服务端聚合覆盖(ADD_REACTION_RESP 是真相)。
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: true, selfId: "alice")
        var reaction = Chirp_Chat_MessageReaction()
        reaction.messageID = "m1"
        reaction.emoji = "👍"
        reaction.count = 5
        reaction.userIds = ["alice", "bob", "carol", "dave", "erin"]
        reaction.reactedByMe = true
        index.applyAggregate(messageId: "m1", reaction: reaction, selfId: "alice")
        let tallies = index.tallies(messageId: "m1")
        XCTAssertEqual(tallies.count, 1)
        XCTAssertEqual(tallies[0].count, 5, "聚合覆盖而非增量叠加")
        XCTAssertTrue(tallies[0].isMine)
        // 聚合 count=0 / 空 emoji 不落(清零语义与事件流一致)。
        var zero = Chirp_Chat_MessageReaction()
        zero.emoji = "👍"
        zero.count = 0
        index.applyAggregate(messageId: "m1", reaction: zero, selfId: "alice")
        XCTAssertEqual(index.tallies(messageId: "m1")[0].count, 5)
    }

    func testMultipleEmojisKeepInsertionOrder() {
        index.record(messageId: "m1", emoji: "🎉", userId: "bob", added: true, selfId: "alice")
        index.record(messageId: "m1", emoji: "👍", userId: "bob", added: true, selfId: "alice")
        let tallies = index.tallies(messageId: "m1")
        XCTAssertEqual(tallies.map(\.emoji), ["🎉", "👍"], "展示序=首次到达序")
    }
}
