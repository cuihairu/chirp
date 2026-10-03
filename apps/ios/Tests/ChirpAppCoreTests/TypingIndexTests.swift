import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 输入状态数据面(P4a):排自己、stop 即清、TTL 6s 惰性过期(web
/// typing_store 同语义)。
final class TypingIndexTests: XCTestCase {
    private var index: TypingIndex!

    override func setUp() {
        super.setUp()
        index = TypingIndex()
    }

    private func indicator(
        user: String, typing: Bool, at: Int64
    ) -> Chirp_Chat_TypingIndicator {
        var value = Chirp_Chat_TypingIndicator()
        value.channelID = "alice|bob"
        value.channelType = .private
        value.userID = user
        value.username = user
        value.isTyping = typing
        value.timestamp = at
        return value
    }

    func testSelfReportsAreExcluded() {
        index.record(indicator(user: "alice", typing: true, at: 1_000), selfId: "alice")
        XCTAssertTrue(
            index.typists(channelType: .private, channelId: "alice|bob", nowMs: 1_000).isEmpty,
            "自己上报的条目不入表")
    }

    func testStartRecordedStopClears() {
        index.record(indicator(user: "bob", typing: true, at: 1_000), selfId: "alice")
        XCTAssertEqual(
            index.typists(channelType: .private, channelId: "alice|bob", nowMs: 1_000), ["bob"])
        index.record(indicator(user: "bob", typing: false, at: 1_500), selfId: "alice")
        XCTAssertTrue(
            index.typists(channelType: .private, channelId: "alice|bob", nowMs: 1_500).isEmpty)
    }

    func testExpiresAtTtlAndOnlyWithinChannel() {
        index.record(indicator(user: "bob", typing: true, at: 1_000), selfId: "alice")
        // TTL 内(1000+5999)在场。
        XCTAssertEqual(
            index.typists(channelType: .private, channelId: "alice|bob", nowMs: 6_999), ["bob"])
        // 6s 到点过期清掉。
        XCTAssertTrue(
            index.typists(channelType: .private, channelId: "alice|bob", nowMs: 7_000).isEmpty)
        // 同 key 的另一频道互不影响(carol 近时刻上报,7000 时仍在 TTL 内)。
        var other = Chirp_Chat_TypingIndicator()
        other.channelID = "alice|carol"
        other.channelType = .private
        other.userID = "carol"
        other.isTyping = true
        other.timestamp = 6_500
        index.record(other, selfId: "alice")
        XCTAssertEqual(
            index.typists(channelType: .private, channelId: "alice|carol", nowMs: 7_000),
            ["carol"])
    }

    func testChannelKeyIncludesChannelType() {
        XCTAssertNotEqual(
            TypingIndex.channelKey(channelType: .private, channelId: "x"),
            TypingIndex.channelKey(channelType: .guild, channelId: "x"))
    }
}
