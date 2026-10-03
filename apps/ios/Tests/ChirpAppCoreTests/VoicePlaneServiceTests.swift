import ChirpProtos
import ChirpProtocol
import XCTest
@testable import ChirpAppCore

/// 语音面服务(P4d):登录认领在房状态(restore→整表拉取)、join 带
/// 空 sdp_offer、三 notify 拼 roster、mute 本地回显。
final class VoicePlaneServiceTests: XCTestCase {
    private var events: [VoicePlaneService.Event] = []
    private var scheduler: ManualScheduler!

    override func setUp() {
        super.setUp()
        events = []
        scheduler = ManualScheduler()
    }

    private func makeService(factory: @escaping (String) -> WsTransport)
        -> VoicePlaneService
    {
        VoicePlaneService(
            userId: "alice",
            voiceUrl: "ws://127.0.0.1:9001",
            transportFactory: factory,
            scheduler: scheduler,
            random: FakeRandom(),
            emit: { [weak self] in self?.events.append($0) })
    }

    private func participant(
        _ id: String, state: Chirp_Voice_ParticipantState,
        muted: Bool = false, deafened: Bool = false
    ) -> Chirp_Voice_ParticipantInfo {
        var info = Chirp_Voice_ParticipantInfo()
        info.userID = id
        info.state = state
        info.muted = muted
        info.deafened = deafened
        return info
    }

    private func answerLoginOk(_ t: FakeWsTransport) throws {
        let seq = try expectRequest(t, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, seq, ok))
    }

    /// GET_USER_ROOM 应答:空 roomID=不在房;否则链一帧 GET_ROOM_INFO 整表。
    private func answerUserRoom(
        _ t: FakeWsTransport, roomId: String,
        roster: [Chirp_Voice_ParticipantInfo] = []
    ) throws {
        let seq = try expectRequest(t, .getUserRoomReq)
        let request = try Chirp_Voice_GetUserRoomRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        var resp = Chirp_Voice_GetUserRoomResponse()
        resp.code = .ok
        resp.roomID = roomId
        t.deliver(try wsResponse(.getUserRoomResp, seq, resp))
        if !roomId.isEmpty {
            let infoSeq = try expectRequest(t, .getRoomInfoReq)
            var info = Chirp_Voice_GetRoomInfoResponse()
            info.code = .ok
            info.roomID = roomId
            info.roomName = "raid"
            info.participants = roster
            t.deliver(try wsResponse(.getRoomInfoResp, infoSeq, info))
        }
    }

    func testLoginRestoresRoomViaFullRoster() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.login()
        try answerLoginOk(t)
        try answerUserRoom(
            t, roomId: "r1",
            roster: [
                participant("alice", state: .connected, muted: true),
                participant("bob", state: .connected),
            ])
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.roomIndex.roomId(), "r1")
        XCTAssertEqual(service.roomIndex.room()?.participants.map(\.userId), ["alice", "bob"])
        XCTAssertEqual(
            service.roomIndex.room()?.participants.first?.muted, true,
            "整表拉取是标志细节的唯一来源")
        XCTAssertTrue(events.contains { if case .loggedIn = $0 { return true }; return false })
        service.shutdown()
    }

    func testLoginWithoutRoomClearsMirror() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.login()
        try answerLoginOk(t)
        try answerUserRoom(t, roomId: "")
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertNil(service.roomIndex.room())
        service.shutdown()
    }

    func testJoinRoomSendsEmptySdpOfferThenRefreshes() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerUserRoom(t, roomId: "")

        let pending = service.joinRoom(roomId: "r2")
        let seq = try expectRequest(t, .joinRoomReq)
        let request = try Chirp_Voice_JoinRoomRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.roomID, "r2")
        XCTAssertEqual(request.sdpOffer, "", "伴侣无媒体面,join 只发空 offer(web 同口径)")
        var ok = Chirp_Voice_JoinRoomResponse()
        ok.code = .ok
        ok.roomID = "r2"
        t.deliver(try wsResponse(.joinRoomResp, seq, ok))
        try answerRoster(t, roomId: "r2", roster: [participant("alice", state: .connected)])
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.roomIndex.roomId(), "r2")
        service.shutdown()
    }

    func testParticipantNotifiesMaintainRoster() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerUserRoom(
            t, roomId: "r1", roster: [participant("alice", state: .connected)])

        // 别人进房(操作者被排除,永不是自己)。
        var joined = Chirp_Voice_ParticipantJoinedNotify()
        joined.roomID = "r1"
        joined.participant = participant("bob", state: .connected)
        t.deliver(try wsNotify(.participantJoinedNotify, joined))
        XCTAssertEqual(service.roomIndex.room()?.participants.map(\.userId), ["alice", "bob"])

        // 陈旧 roomId 的 joined 不得误插。
        var staleJoined = Chirp_Voice_ParticipantJoinedNotify()
        staleJoined.roomID = "r-other"
        staleJoined.participant = participant("eve", state: .connected)
        t.deliver(try wsNotify(.participantJoinedNotify, staleJoined))
        XCTAssertEqual(service.roomIndex.room()?.participants.count, 2)

        // 状态变化:只改 state。
        var stateChanged = Chirp_Voice_ParticipantStateChangedNotify()
        stateChanged.roomID = "r1"
        stateChanged.userID = "bob"
        stateChanged.state = .muted
        t.deliver(try wsNotify(.participantStateChangedNotify, stateChanged))
        XCTAssertEqual(
            service.roomIndex.room()?.participants.first { $0.userId == "bob" }?.state, .muted)

        // 离房:移除;陈旧 roomId 的 left 忽略。
        var left = Chirp_Voice_ParticipantLeftNotify()
        left.roomID = "r1"
        left.userID = "bob"
        t.deliver(try wsNotify(.participantLeftNotify, left))
        XCTAssertEqual(service.roomIndex.room()?.participants.map(\.userId), ["alice"])
        var staleLeft = Chirp_Voice_ParticipantLeftNotify()
        staleLeft.roomID = "r-other"
        staleLeft.userID = "alice"
        t.deliver(try wsNotify(.participantLeftNotify, staleLeft))
        XCTAssertEqual(service.roomIndex.room()?.participants.map(\.userId), ["alice"])
        service.shutdown()
    }

    func testSetMuteAppliesLocalEcho() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerUserRoom(
            t, roomId: "r1", roster: [participant("alice", state: .connected)])

        let pending = service.setMute(true)
        let seq = try expectRequest(t, .setMuteReq)
        let request = try Chirp_Voice_SetMuteRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.roomID, "r1")
        XCTAssertEqual(request.muted, true)
        var ok = Chirp_Voice_SetMuteResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.setMuteResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(
            service.roomIndex.room()?.participants.first?.muted, true,
            "state 广播排除操作者,应答 ok 即本地回显")
        service.shutdown()
    }

    func testLeaveRoomClearsMirror() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        _ = service.login()
        try answerLoginOk(t)
        try answerUserRoom(
            t, roomId: "r1", roster: [participant("alice", state: .connected)])

        let pending = service.leaveRoom()
        let seq = try expectRequest(t, .leaveRoomReq)
        var ok = Chirp_Voice_LeaveRoomResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.leaveRoomResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), true)
        XCTAssertNil(service.roomIndex.room())
        service.shutdown()
    }

    /// joinRoom 之后的 GET_ROOM_INFO 整表应答(answerUserRoom 已含同款
    /// 逻辑,这里独立出来给 join 用例复用)。
    private func answerRoster(
        _ t: FakeWsTransport, roomId: String,
        roster: [Chirp_Voice_ParticipantInfo]
    ) throws {
        let seq = try expectRequest(t, .getRoomInfoReq)
        let request = try Chirp_Voice_GetRoomInfoRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.roomID, roomId)
        var info = Chirp_Voice_GetRoomInfoResponse()
        info.code = .ok
        info.roomID = roomId
        info.participants = roster
        t.deliver(try wsResponse(.getRoomInfoResp, seq, info))
    }
}
