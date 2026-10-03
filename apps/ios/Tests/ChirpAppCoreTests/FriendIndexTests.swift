import XCTest
@testable import ChirpAppCore

/// 好友名册数据面(P4c):字典序、幂等插入/删除、入向申请去重、纯本地出向。
final class FriendIndexTests: XCTestCase {
    private var index: FriendIndex!

    override func setUp() {
        super.setUp()
        index = FriendIndex()
    }

    func testReplaceFriendsSortsLexicographically() {
        index.replaceFriends(["u3", "u1", "u2"])
        XCTAssertEqual(index.friends(), ["u1", "u2", "u3"])
    }

    func testReplaceFriendsWithSameContentIsIdempotent() {
        index.replaceFriends(["u2", "u1"])
        index.replaceFriends(["u1", "u2"])
        index.addFriend("u2")
        XCTAssertEqual(
            index.friends(), ["u1", "u2"], "已存在项重复插入不应重复")
    }

    func testAddAndRemoveFriendAreIdempotent() {
        index.addFriend("u1")
        index.addFriend("u1")
        XCTAssertEqual(index.friends(), ["u1"])
        index.removeFriend("u1")
        index.removeFriend("u1")
        XCTAssertTrue(index.friends().isEmpty)
    }

    func testAddFriendKeepsSortedOrder() {
        index.replaceFriends(["a", "m"])
        index.addFriend("f")
        XCTAssertEqual(index.friends(), ["a", "f", "m"])
    }

    func testPendingInDeduplicatesByRequestId() {
        index.addPendingIn(requestId: "r1", fromUserId: "u1")
        index.addPendingIn(requestId: "r1", fromUserId: "u1")
        index.addPendingIn(requestId: "r2", fromUserId: "u2")
        XCTAssertEqual(
            index.pendingIn().map(\.requestId), ["r1", "r2"], "保到达序")
    }

    func testPendingInPreservesServerArrivalOrderOnReplace() {
        // 整表换入:保服务端给的序,不排序。
        index.replacePendingIn([
            .init(requestId: "z", fromUserId: "u9"),
            .init(requestId: "a", fromUserId: "u1"),
        ])
        XCTAssertEqual(index.pendingIn().map(\.requestId), ["z", "a"])
    }

    func testResolvePendingRemovesOnlyThatRequest() {
        index.addPendingIn(requestId: "r1", fromUserId: "u1")
        index.addPendingIn(requestId: "r2", fromUserId: "u2")
        index.resolvePending(requestId: "r1")
        index.resolvePending(requestId: "r1")
        XCTAssertEqual(index.pendingIn().map(\.requestId), ["r2"])
    }

    func testPendingOutSkipsFriendsAndDuplicates() {
        index.replaceFriends(["u1"])
        index.addPendingOut("u1")
        XCTAssertTrue(index.pendingOut().isEmpty, "已是好友不再记出向申请")
        index.addPendingOut("u2")
        index.addPendingOut("u2")
        XCTAssertEqual(index.pendingOut(), ["u2"])
        // 对方接受后进名册,出向记录随之让位。
        index.addFriend("u2")
        XCTAssertEqual(
            index.friends(), ["u1", "u2"],
            "出向记录独立于名册,名册以服务端 notify 为准")
    }

    func testResetDropsEverything() {
        index.replaceFriends(["u1"])
        index.addPendingIn(requestId: "r1", fromUserId: "u2")
        index.addPendingOut("u3")
        index.reset()
        XCTAssertTrue(index.friends().isEmpty)
        XCTAssertTrue(index.pendingIn().isEmpty)
        XCTAssertTrue(index.pendingOut().isEmpty)
        index.addFriend("new")
        XCTAssertEqual(index.friends(), ["new"])
    }
}