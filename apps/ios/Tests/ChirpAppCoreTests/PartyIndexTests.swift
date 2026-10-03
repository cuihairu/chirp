import XCTest
@testable import ChirpAppCore

/// 组队镜像(P4d):快照换入/清空、邀请队列去重与重置、队长位判定。
final class PartyIndexTests: XCTestCase {
    private func snapshot(
        _ id: String, leader: String, members: [(String, Bool)]
    ) -> PartyIndex.Snapshot {
        PartyIndex.Snapshot(
            partyId: id, leaderId: leader, maxMembers: 5,
            members: members.map { PartyIndex.Member(userId: $0.0, ready: $0.1) })
    }

    func testApplyAndClearRoundTrip() {
        let index = PartyIndex()
        XCTAssertNil(index.party())
        index.apply(snapshot("p1", leader: "alice", members: [("alice", true), ("bob", false)]))
        XCTAssertEqual(index.party()?.partyId, "p1")
        XCTAssertEqual(index.party()?.members.map(\.userId), ["alice", "bob"])
        index.clear()
        XCTAssertNil(index.party())
    }

    func testInviteQueueDedupesById() {
        let index = PartyIndex()
        index.addInvite(.init(inviteId: "i1", fromUserId: "alice", partyId: "p1"))
        index.addInvite(.init(inviteId: "i1", fromUserId: "alice", partyId: "p1"))
        index.addInvite(.init(inviteId: "i2", fromUserId: "carol", partyId: "p2"))
        XCTAssertEqual(index.invites().map(\.inviteId), ["i1", "i2"])
        index.removeInvite(inviteId: "i1")
        XCTAssertEqual(index.invites().map(\.inviteId), ["i2"])
    }

    func testResetInvitesDropsEverything() {
        let index = PartyIndex()
        index.addInvite(.init(inviteId: "i1", fromUserId: "alice", partyId: "p1"))
        index.resetInvites()
        XCTAssertTrue(index.invites().isEmpty, "换号/重登不得泄漏上一账号的邀请")
    }

    func testLeaderAndSelfMemberLookups() {
        let index = PartyIndex()
        index.apply(snapshot("p1", leader: "alice", members: [("alice", true), ("bob", false)]))
        XCTAssertTrue(index.isLeader(userId: "alice"))
        XCTAssertFalse(index.isLeader(userId: "bob"))
        XCTAssertFalse(index.isLeader(userId: "nobody"))
        XCTAssertEqual(index.selfMember(userId: "bob")?.ready, false)
        XCTAssertNil(index.selfMember(userId: "nobody"))
    }
}
