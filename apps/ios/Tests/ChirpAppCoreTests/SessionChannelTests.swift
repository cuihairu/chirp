import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 导航键反解与频道类型映射(P4e;键形态与 web models.ts 逐字同构)。
final class SessionChannelTests: XCTestCase {
    func testDmKeyRoundTripsFromBothSides() {
        let channel = SessionChannel.dm(selfId: "alice", peerId: "bob")
        XCTAssertEqual(channel.key, "p:alice|bob")
        XCTAssertEqual(channel.channelId, "alice|bob")
        XCTAssertEqual(channel.peerId, "bob")
        XCTAssertEqual(channel.channelType, .private)

        // 两侧反解都得到对端=另一侧。
        XCTAssertEqual(SessionChannel(key: "p:alice|bob", selfId: "alice")?.peerId, "bob")
        XCTAssertEqual(SessionChannel(key: "p:alice|bob", selfId: "bob")?.peerId, "alice")
        XCTAssertEqual(SessionChannel(key: "p:alice|bob", selfId: "carol")?.peerId, "alice",
            "旁观者拿先位当对端(伴侣面不会出现,口径钉死)")
    }

    func testGroupKeyRoundTrips() {
        let channel = SessionChannel.group("g1")
        XCTAssertEqual(channel.key, "g:g1")
        XCTAssertEqual(channel.channelId, "g1")
        XCTAssertEqual(channel.peerId, "g1")
        XCTAssertEqual(channel.channelType, .guild)
        XCTAssertEqual(SessionChannel(key: "g:g1", selfId: "alice")?.peerId, "g1")
        XCTAssertEqual(SessionChannel(key: "g:g1", selfId: "alice")?.kind, .group)
    }

    func testMalformedKeysRejected() {
        XCTAssertNil(SessionChannel(key: "alice|bob", selfId: "alice"), "无前缀")
        XCTAssertNil(SessionChannel(key: "team:t1", selfId: "alice"), "未知前缀")
        XCTAssertNil(SessionChannel(key: "g:", selfId: "alice"), "空群 id")
        XCTAssertNil(SessionChannel(key: "p:", selfId: "alice"), "空对")
        XCTAssertNil(SessionChannel(key: "p:a|b|c", selfId: "alice"), "三段不是对")
        XCTAssertNil(SessionChannel(key: "", selfId: "alice"))
    }
}
