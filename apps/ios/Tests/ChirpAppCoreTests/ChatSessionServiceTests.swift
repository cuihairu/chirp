import ChirpProtos
import ChirpProtocol
import XCTest
@testable import ChirpAppCore

/// P2 最小闭环的服务级向量:假传输上跑通 登录→收消息(回执)→发送→
/// 断线入队→重连重放。驱动手法与 ChirpProtocolTests 同款(同步 open、
/// 手工 deliver、ManualScheduler 控重连节拍)。
final class ChatSessionServiceTests: XCTestCase {
    private var events: [ChatServiceEvent] = []
    private var scheduler: ManualScheduler!

    override func setUp() {
        super.setUp()
        events = []
        scheduler = ManualScheduler()
    }

    private func makeService(
        factory: @escaping (String) -> WsTransport
    ) -> ChatSessionService {
        ChatSessionService(
            userId: "alice",
            deviceId: "ios-test",
            chatUrl: "ws://127.0.0.1:7001",
            transportFactory: factory,
            scheduler: scheduler,
            random: FakeRandom(),
            // now 固定 1000 —— 回执 receivedAt 与发送侧存档时间戳可断言。
            now: { 1_000 }
        ) { [weak self] in self?.events.append($0) }
    }

    /// 单传输工厂(无重连场景);测试直接持有 t 读帧/注帧。
    private func singleTransport() -> (FakeWsTransport, (String) -> WsTransport) {
        let t = FakeWsTransport()
        return (t, { _ in t })
    }

    /// 登录到 OK:请求帧带 token/deviceId/platform,终态事件 loginSucceeded。
    private func loginOk(_ service: ChatSessionService, _ transport: FakeWsTransport) throws {
        let pending = service.login()
        let seq = try expectRequest(transport, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        transport.deliver(try wsResponse(.loginResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertTrue(events.contains { if case .loginSucceeded = $0 { return true }; return false })
    }

    func testLoginRoundTripOk() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        let pending = service.login()
        let seq = try expectRequest(t, .loginReq)
        // 请求载荷:dev 阶段用户名即 token;platform 上报 ios。
        let request = try Chirp_Auth_LoginRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.token, "alice")
        XCTAssertEqual(request.deviceID, "ios-test")
        XCTAssertEqual(request.platform, "ios")

        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.connectionState, .connected)
        service.logout()
    }

    func testLoginRejectedSurfacesEvent() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        let pending = service.login()
        let seq = try expectRequest(t, .loginReq)
        var rejected = Chirp_Auth_LoginResponse()
        rejected.code = .authFailed
        t.deliver(try wsResponse(.loginResp, seq, rejected))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .authFailed)
        XCTAssertTrue(events.contains {
            if case .loginRejected(let code) = $0 { return code == .authFailed }
            return false
        })
        service.logout()
    }

    func testIncomingNotifySendsAckBeforeIndexing() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let before = t.sent.count
        t.deliver(try wsNotify(.chatMessageNotify, dmText("bob", "alice", id: "m1", text: "在吗", ts: 900)))

        // 回执:MESSAGE_ACK,echo messageId,user=自己,receivedAt=注入时钟。
        let ackPacket = try sentPacket(t)
        XCTAssertEqual(ackPacket.msgID, .messageAck)
        XCTAssertTrue(t.sent.count > before)
        let ack = try Chirp_Chat_MessageAck(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(ack.messageID, "m1")
        XCTAssertEqual(ack.userID, "alice")
        XCTAssertEqual(ack.receivedAt, 1_000)

        // 会话索引与事件。
        XCTAssertEqual(service.sessions.summary(peerId: "bob")?.unread, 1)
        XCTAssertTrue(events.contains {
            if case .messageReceived(let m) = $0 { return m.messageID == "m1" }
            return false
        })
        service.logout()
    }

    func testSendDmRoundTripAndArchive() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.send(peerId: "bob", content: "你好")
        let seq = try expectRequest(t, .sendMessageReq)
        let request = try Chirp_Chat_SendMessageRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.receiverID, "bob")
        XCTAssertEqual(request.channelID, "alice|bob", "DM 频道=排序对拼接")

        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.sendMessageResp, seq, ok))
        guard case .sent = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .sent")
        }
        // 会话预览(发出侧不涨未读)+ 存档可查(store 侧存档副本 messageID 空)。
        let summary = service.sessions.summary(peerId: "bob")
        XCTAssertEqual(summary?.lastMessage, "你好")
        XCTAssertEqual(summary?.unread, 0)
        let history = service.history(peerId: "bob")
        XCTAssertEqual(history.count, 1)
        XCTAssertEqual(history.first?.messageID, "")
        XCTAssertEqual(history.first?.channelID, "alice|bob")
        service.logout()
    }

    func testSendWhileOfflineQueuesAndReplaysAfterReconnect() throws {
        let first = FakeWsTransport()
        var second: FakeWsTransport?
        var factoryCalls = 0
        let service = makeService { _ in
            factoryCalls += 1
            if factoryCalls == 1 { return first }  // 首连
            if second == nil { second = FakeWsTransport() }  // 重连新传输
            return second!
        }
        try loginOk(service, first)

        // 断线(不走本地 close 挥手):待发请求被冲刷为 CLOSED。
        first.dropLink()
        scheduler.advance(0)

        let pending = service.send(peerId: "bob", content: "离线消息")
        guard case .queuedOffline = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .queuedOffline")
        }
        XCTAssertTrue(events.contains {
            if case .offlineQueued(let content) = $0 { return content == "离线消息" }
            return false
        })

        // 退避首跳(base 500ms,jitter=0)后自动重连;新传输上重放队列。
        scheduler.advance(600)
        guard let t2 = second else { return XCTFail("no reconnect transport") }
        let seq = try expectRequest(t2, .sendMessageReq)
        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t2.deliver(try wsResponse(.sendMessageResp, seq, ok))
        XCTAssertTrue(events.contains {
            if case .offlineReplayed(let count) = $0 { return count == 1 }
            return false
        })
        XCTAssertEqual(service.sessions.summary(peerId: "bob")?.lastMessage, "离线消息")
        service.logout()
    }

    func testKickNotifiesAndBlocksAutoReconnect() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        var kick = Chirp_Auth_KickNotify()
        kick.reason = "elsewhere"
        t.deliver(try wsNotify(.kickNotify, kick))
        XCTAssertTrue(events.contains {
            if case .kicked(let reason) = $0 { return reason == "elsewhere" }
            return false
        })
        XCTAssertEqual(service.connectionState, .kicked)
        service.logout()
    }
}
