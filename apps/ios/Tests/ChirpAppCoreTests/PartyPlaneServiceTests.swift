import ChirpProtos
import ChirpProtocol
import XCTest
@testable import ChirpAppCore

/// 组队面服务(P4d):登录拉快照、STATE_CHANGED 快照换入、被踢清空、
/// 邀请入向队列、退队应答语义。
final class PartyPlaneServiceTests: XCTestCase {
    private var events: [PartyPlaneService.Event] = []
    private var scheduler: ManualScheduler!

    override func setUp() {
        super.setUp()
        events = []
        scheduler = ManualScheduler()
    }

    private func makeService(factory: @escaping (String) -> WsTransport)
        -> PartyPlaneService
    {
        PartyPlaneService(
            userId: "alice",
            partyUrl: "ws://127.0.0.1:7501",
            transportFactory: factory,
            scheduler: scheduler,
            random: FakeRandom(),
            emit: { [weak self] in self?.events.append($0) })
    }

    private func partyInfo(
        _ id: String, leader: String, members: [(String, Bool)]
    ) -> Chirp_Party_PartyInfo {
        var info = Chirp_Party_PartyInfo()
        info.partyID = id
        info.leaderID = leader
        info.maxMembers = 5
        info.members = members.map {
            var member = Chirp_Party_PartyMember()
            member.userID = $0.0
            member.ready = $0.1
            return member
        }
        return info
    }

    func testLoginPullsSnapshotAndEmitsLoggedIn() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.login()
        // LOGIN → ok
        let loginSeq = try expectRequest(t, .loginReq)
        let loginBody = try Chirp_Auth_LoginRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(loginBody.platform, "ios")
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, loginSeq, ok))
        // GET_MY_PARTY → 在队快照
        let seq = try expectRequest(t, .getMyPartyReq)
        let request = try Chirp_Party_GetMyPartyRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        var resp = Chirp_Party_GetMyPartyResponse()
        resp.code = .ok
        resp.inParty = true
        resp.party = partyInfo("p1", leader: "alice", members: [("alice", true)])
        t.deliver(try wsResponse(.getMyPartyResp, seq, resp))

        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.party.party()?.partyId, "p1")
        XCTAssertTrue(events.contains { if case .loggedIn = $0 { return true }; return false })
        service.shutdown()
    }

    func testLoginWithoutPartyClearsMirror() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.login()
        let loginSeq = try expectRequest(t, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, loginSeq, ok))
        let seq = try expectRequest(t, .getMyPartyReq)
        var resp = Chirp_Party_GetMyPartyResponse()
        resp.code = .ok
        resp.inParty = false
        t.deliver(try wsResponse(.getMyPartyResp, seq, resp))

        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertNil(service.party.party())
        service.shutdown()
    }

    func testStateChangedNotifyReplacesSnapshot() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerGetMyParty(t, inParty: false)

        var notify = Chirp_Party_PartyStateChangedNotify()
        notify.party = partyInfo("p9", leader: "carol", members: [("carol", false), ("alice", true)])
        t.deliver(try wsNotify(.partyStateChangedNotify, notify))
        XCTAssertEqual(service.party.party()?.partyId, "p9")
        XCTAssertFalse(service.party.isLeader(userId: "alice"))

        // 被踢:清空(被踢者收不到快照,只收得到 kicked)。
        var kicked = Chirp_Party_PartyKickedNotify()
        kicked.partyID = "p9"
        t.deliver(try wsNotify(.partyKickedNotify, kicked))
        XCTAssertNil(service.party.party())
        service.shutdown()
    }

    func testInviteNotifyQueuesDeduped() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerGetMyParty(t, inParty: false)

        var notify = Chirp_Party_InviteNotify()
        notify.inviteID = "inv-1"
        notify.fromUserID = "carol"
        notify.party = partyInfo("p9", leader: "carol", members: [("carol", false)])
        t.deliver(try wsNotify(.inviteNotify, notify))
        t.deliver(try wsNotify(.inviteNotify, notify))
        XCTAssertEqual(service.party.invites().map(\.inviteId), ["inv-1"])
        XCTAssertEqual(service.party.invites().first?.fromUserId, "carol")
        service.shutdown()
    }

    func testLeavePartyReportsDisband() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerGetMyParty(
            t, inParty: true, party: partyInfo("p1", leader: "alice", members: [("alice", true)]))

        let pending = service.leaveParty()
        let seq = try expectRequest(t, .leavePartyReq)
        let request = try Chirp_Party_LeavePartyRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.partyID, "p1")
        var resp = Chirp_Party_LeavePartyResponse()
        resp.code = .ok
        resp.partyDisbanded = true
        t.deliver(try wsResponse(.leavePartyResp, seq, resp))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), true, "最后一人离开=解散")
        XCTAssertNil(service.party.party(), "退队即清镜像")
        service.shutdown()
    }

    // ---- 共用驱动 ------------------------------------------------------------

    private func answerLoginOk(_ t: FakeWsTransport) throws {
        let seq = try expectRequest(t, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, seq, ok))
    }

    private func answerGetMyParty(
        _ t: FakeWsTransport, inParty: Bool,
        party: Chirp_Party_PartyInfo = Chirp_Party_PartyInfo()
    ) throws {
        let seq = try expectRequest(t, .getMyPartyReq)
        var resp = Chirp_Party_GetMyPartyResponse()
        resp.code = .ok
        resp.inParty = inParty
        if inParty { resp.party = party }
        t.deliver(try wsResponse(.getMyPartyResp, seq, resp))
    }
}
