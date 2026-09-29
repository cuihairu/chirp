import ChirpProtos
import XCTest
@testable import ChirpProtocol

/// Same vector group as Kotlin HooksTest (MemoryMessageStore +
/// WordFilterLoader).
final class HooksTests: XCTestCase {
    private func message(
        _ content: String,
        channelType: Chirp_Chat_ChannelType = .`private`,
        channelId: String = "a|b",
        timestamp: Int64 = 5_000
    ) -> Chirp_Chat_ChatMessage {
        var m = Chirp_Chat_ChatMessage()
        m.senderID = "alice"
        m.channelType = channelType
        m.channelID = channelId
        m.msgType = .text
        m.content = Data(content.utf8)
        m.timestamp = timestamp
        return m
    }

    private func contents(_ page: [Chirp_Chat_ChatMessage]) -> [String] {
        page.map { String(decoding: $0.content, as: UTF8.self) }
    }

    // ---- MemoryMessageStore -------------------------------------------------

    func testStoreLoadsNewestFirstAndEvictsOldestBeyondCap() throws {
        let store = MemoryMessageStore(maxPerChannel: 2)
        try store.save(message("one", timestamp: 1))
        try store.save(message("two", timestamp: 2))
        try store.save(message("three", timestamp: 3))
        let page = store.load(channelType: .`private`, channelId: "a|b", limit: 10)
        XCTAssertEqual(["three", "two"], contents(page))
    }

    func testStoreKeysPerChannelTypeAndChannelId() throws {
        let store = MemoryMessageStore()
        try store.save(message("private", channelId: "a|b"))
        try store.save(message("world", channelType: .world, channelId: "world"))
        XCTAssertEqual(1, store.load(channelType: .`private`, channelId: "a|b", limit: 10).count)
        XCTAssertEqual(1, store.load(channelType: .world, channelId: "world", limit: 10).count)
        XCTAssertEqual(0, store.load(channelType: .`private`, channelId: "world", limit: 10).count)
    }

    func testStoreHonorsLimitAndBeforeTimestamp() throws {
        let store = MemoryMessageStore()
        try store.save(message("one", timestamp: 1))
        try store.save(message("two", timestamp: 2))
        try store.save(message("three", timestamp: 3))
        XCTAssertEqual(
            ["three", "two"],
            contents(store.load(channelType: .`private`, channelId: "a|b", limit: 2)))
        XCTAssertEqual(
            ["two", "one"],
            contents(store.load(
                channelType: .`private`, channelId: "a|b", limit: 10, beforeTimestamp: 3)))
        XCTAssertEqual(
            0, store.load(channelType: .`private`, channelId: "a|b", limit: 0).count)
    }

    func testStoreCleanupDropsOldEntriesAndEmptyChannels() throws {
        let store = MemoryMessageStore()
        try store.save(message("old-private", channelId: "a|b", timestamp: 10))
        try store.save(message("old-world", channelType: .world, channelId: "world", timestamp: 20))
        try store.save(message("fresh", channelId: "a|b", timestamp: 500))
        try store.cleanup(olderThanMs: 100)
        XCTAssertEqual(
            ["fresh"],
            contents(store.load(channelType: .`private`, channelId: "a|b", limit: 10)))
        XCTAssertEqual(
            0, store.load(channelType: .world, channelId: "world", limit: 10).count)
    }

    // ---- WordFilterLoader ---------------------------------------------------

    func testLoaderReadsServerFormatLexicon() {
        let filter = WordFilterLoader.load("Spam\r\n# 注释行\n\n  dummy \n论坛\n")
        XCTAssertEqual(["dummy", "spam", "论坛"], filter.terms)
        let result = filter.filter("hey SPAM now")
        XCTAssertTrue(result.allowed)
        XCTAssertEqual("hey ** now", result.content)
    }

    func testLoaderHonorsRejectPolicy() {
        let filter = WordFilterLoader.load("banned\n", policy: .reject)
        XCTAssertFalse(filter.filter("totally BANNED words").allowed)
    }
}
