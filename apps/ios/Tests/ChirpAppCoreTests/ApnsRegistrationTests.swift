import ChirpProtos
import ChirpProtocol
import XCTest
@testable import ChirpAppCore

final class AwaitedPushTokenSourceTests: XCTestCase {
    func testOfferBeforeFetchDeliversImmediately() throws {
        let scheduler = ManualScheduler()
        let source = AwaitedPushTokenSource(timeoutMs: 10_000, scheduler: scheduler)
        source.offer("tok-1")
        var reported: String?
        try source.fetchToken { reported = $0 }
        XCTAssertEqual(reported, "tok-1")
    }

    func testOfferAfterFetchDeliversOnCallbackThread() throws {
        let scheduler = ManualScheduler()
        let source = AwaitedPushTokenSource(timeoutMs: 10_000, scheduler: scheduler)
        var reported: String?
        try source.fetchToken { reported = $0 }
        XCTAssertNil(reported, "尚未 offer 前不回调")
        source.offer("tok-2")
        XCTAssertEqual(reported, "tok-2")
    }

    func testTimeoutDegradesToNil() throws {
        let scheduler = ManualScheduler()
        let source = AwaitedPushTokenSource(timeoutMs: 10_000, scheduler: scheduler)
        var reported: String?
        try source.fetchToken { reported = $0 }
        scheduler.advance(10_000)
        XCTAssertNil(reported, "超时=空 token 降级")
        source.offer("late")  // 迟到的系统回调:丢弃,不再二次回调
        XCTAssertNil(reported)
    }

    func testNilOfferIsAFailureDegrade() throws {
        let scheduler = ManualScheduler()
        let source = AwaitedPushTokenSource(timeoutMs: 10_000, scheduler: scheduler)
        var reported: String?
        try source.fetchToken { reported = $0 }
        source.offer(nil)  // didFailToRegister...
        XCTAssertNil(reported)
    }
}

final class DevicePlaneServiceTests: XCTestCase {
    private var events: [DevicePlaneService.Event] = []
    private var scheduler: ManualScheduler!

    override func setUp() {
        super.setUp()
        events = []
        scheduler = ManualScheduler()
    }

    private func makeService(factory: @escaping (String) -> WsTransport)
        -> DevicePlaneService
    {
        DevicePlaneService(
            userId: "alice",
            deviceId: "ios-test",
            deviceUrl: "ws://127.0.0.1:5201",
            appVersion: "0.0.1",
            transportFactory: factory,
            scheduler: scheduler,
            random: FakeRandom(),
            emit: { [weak self] in self?.events.append($0) })
    }

    /// 设备面 LOGIN 的成功应答。
    private func answerLogin(_ transport: FakeWsTransport) throws {
        let seq = try expectRequest(transport, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        transport.deliver(try wsResponse(.loginResp, seq, ok))
    }

    private func answerRegister(_ transport: FakeWsTransport, code: Chirp_Common_ErrorCode) throws {
        let seq = try expectRequest(transport, .registerDeviceReq)
        var resp = Chirp_AppNotification_RegisterDeviceResponse()
        resp.code = code
        transport.deliver(try wsResponse(.registerDeviceResp, seq, resp))
    }

    func testLoadDevicesRoundTripAfterRegister() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.register()
        try answerLogin(t)
        try answerRegister(t, code: .ok)
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)

        // GET_USER_DEVICES:请求只带 user_id(服务端按会话钉身份)。
        let load = service.loadDevices()
        let seq = try expectRequest(t, .getUserDevicesReq)
        let request = try Chirp_AppNotification_GetUserDevicesRequest(
            serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")

        var resp = Chirp_AppNotification_GetUserDevicesResponse()
        resp.code = .ok
        var device = Chirp_AppNotification_DeviceInfo()
        device.deviceID = "ios-test"
        device.platform = "ios"
        device.isActive = true
        var web = Chirp_AppNotification_DeviceInfo()
        web.deviceID = "web-1"
        web.platform = "web"
        resp.devices = [device, web]
        t.deliver(try wsResponse(.getUserDevicesResp, seq, resp))

        let loaded = try load.get(timeoutSeconds: 2)
        XCTAssertEqual(loaded.code, .ok)
        XCTAssertEqual(loaded.devices.map(\.platform), ["ios", "web"])
        service.shutdown()
    }

    func testLoadDevicesFailsWhenPlaneDisconnected() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        // 未连接即调:直接 CLOSED 终态失败,不上线路。
        do {
            _ = try service.loadDevices().get(timeoutSeconds: 2)
            XCTFail("断开时应失败")
        } catch {
            XCTAssertTrue(error is RequestError)
        }
        service.shutdown()
    }

    func testRegisterHappyPathFillsApnsSlot() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }

        // 同步就绪的 token 源(offer 先行)。
        let tokenSource = AwaitedPushTokenSource(timeoutMs: 10_000, scheduler: scheduler)
        tokenSource.offer("apns-token-1")

        let pending = service.register(tokenSource: tokenSource)
        try answerLogin(t)
        // REGISTER_DEVICE:apns 槽位(非 fcm),平台/设备/版本字段齐。
        let seq = try expectRequest(t, .registerDeviceReq)
        let request = try Chirp_AppNotification_RegisterDeviceRequest(
            serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.apnsToken, "apns-token-1")
        XCTAssertEqual(request.fcmToken, "")
        XCTAssertEqual(request.platform, "ios")
        XCTAssertEqual(request.deviceID, "ios-test")
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.appVersion, "0.0.1")

        var ok = Chirp_AppNotification_RegisterDeviceResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.registerDeviceResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertTrue(events.contains { if case .registered = $0 { return true }; return false })
        service.shutdown()
    }

    func testNilTokenStillRegistersDegrade() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        // 无 token 源:空 apns_token 照注册(dart 降级对齐)。
        let pending = service.register()
        try answerLogin(t)
        let seq = try expectRequest(t, .registerDeviceReq)
        let request = try Chirp_AppNotification_RegisterDeviceRequest(
            serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.apnsToken, "")

        var ok = Chirp_AppNotification_RegisterDeviceResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.registerDeviceResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        service.shutdown()
    }

    func testDeviceLoginRejectedStopsBeforeRegister() throws {
        let t = FakeWsTransport()
        let service = makeService { _ in t }
        let pending = service.register()
        let seq = try expectRequest(t, .loginReq)
        var rejected = Chirp_Auth_LoginResponse()
        rejected.code = .authFailed
        t.deliver(try wsResponse(.loginResp, seq, rejected))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .authFailed)
        XCTAssertTrue(events.contains {
            if case .rejected(let code) = $0 { return code == .authFailed }
            return false
        })
        XCTAssertEqual(t.sent.count, 1, "登录被拒后不发送 REGISTER_DEVICE")
        service.shutdown()
    }
}
