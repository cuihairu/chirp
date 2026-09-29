import ChirpProtos
import SwiftProtobuf
import XCTest
@testable import ChirpProtocol

struct RegistrarBoom: Error {}

/// Closure-backed PushTokenSource double (the Kotlin tests used the
/// fun-interface lambda form).
final class TokenSource: PushTokenSource {
    private let body: (@escaping (String?) -> Void) throws -> Void
    init(_ body: @escaping (@escaping (String?) -> Void) throws -> Void) {
        self.body = body
    }
    func fetchToken(onResult: @escaping (String?) -> Void) throws {
        try body(onResult)
    }
}

/// Same vector group as Kotlin DeviceRegistrarTest (7 tests) — dart
/// device_api.dart registerSelf conformance incl. the empty-token
/// degradation path.
final class DeviceRegistrarTests: XCTestCase {
    private let scheduler = ManualScheduler(startMs: 1_000)
    private var transports: [FakeWsTransport] = []

    override func setUp() {
        super.setUp()
        transports = []
    }

    private func connectedRegistrar(
        appVersion: String = "0.1.0",
        osVersion: String = "15",
        deviceName: String = "Google Pixel 8"
    ) throws -> (DeviceRegistrar, FakeWsTransport) {
        let connection = ChatConnection(
            url: "ws://test/chirp",
            transportFactory: { [self] _ in
                let fake = FakeWsTransport()
                transports.append(fake)
                return fake
            },
            scheduler: scheduler
        )
        _ = try connection.connect().get()
        let registrar = DeviceRegistrar(
            conn: connection,
            deviceId: { "device-1" },
            appVersion: appVersion,
            osVersion: { osVersion },
            deviceName: { deviceName }
        )
        return (registrar, transports.last!)
    }

    private func wsMessage(
        _ msgId: Chirp_Gateway_MsgID,
        _ sequence: Int64,
        _ body: SwiftProtobuf.Message
    ) throws -> [UInt8] {
        var packet = Chirp_Gateway_Packet()
        packet.msgID = msgId
        packet.sequence = sequence
        packet.body = try body.serializedData()
        return try encodeFrame(payload: [UInt8](packet.serializedData()))
    }

    private func registerResponse(
        _ sequence: Int64,
        _ code: Chirp_Common_ErrorCode
    ) throws -> [UInt8] {
        var response = Chirp_AppNotification_RegisterDeviceResponse()
        response.code = code
        return try wsMessage(.registerDeviceResp, sequence, response)
    }

    private func sentRequest(_ transport: FakeWsTransport) throws -> Chirp_AppNotification_RegisterDeviceRequest {
        let packet = try Chirp_Gateway_Packet(serializedBytes: Data(transport.lastSentPayload()))
        XCTAssertEqual(Chirp_Gateway_MsgID.registerDeviceReq, packet.msgID)
        return try Chirp_AppNotification_RegisterDeviceRequest(serializedBytes: packet.body)
    }

    // ---- token sourcing -----------------------------------------------------

    func testRegisterCarriesAllFieldsAndPropagatesOk() throws {
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(
            userId: "user-1",
            pushToken: TokenSource { onResult in onResult("fcm-token-1") })
        let request = try sentRequest(transport)
        XCTAssertEqual("user-1", request.userID)
        XCTAssertEqual("device-1", request.deviceID)
        // Platform identity is per-device ("ios" here, "android" in the
        // Kotlin vector) — the only intentional vector divergence.
        XCTAssertEqual("ios", request.platform)
        XCTAssertEqual("fcm-token-1", request.fcmToken)
        XCTAssertEqual("0.1.0", request.appVersion)
        XCTAssertEqual("15", request.osVersion)
        XCTAssertEqual("Google Pixel 8", request.deviceName)
        transport.deliverWsMessage(try registerResponse(1, .ok))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
    }

    func testNullTokenSourceStillRegistersWithEmptyToken() throws {
        // dart 降级对齐：registerSelf 在无推送 token 时照样注册（空
        // fcm_token），设备仍清单化，推送退化为服务端日志投递。
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(userId: "user-1", pushToken: nil)
        let request = try sentRequest(transport)
        XCTAssertEqual("", request.fcmToken)
        XCTAssertEqual("device-1", request.deviceID)
        transport.deliverWsMessage(try registerResponse(1, .ok))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
    }

    func testFailingLookupDegradesToEmptyTokenButStillRegisters() throws {
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(
            userId: "user-1",
            pushToken: TokenSource { onResult in onResult(nil) })
        XCTAssertEqual("", try sentRequest(transport).fcmToken)
        transport.deliverWsMessage(try registerResponse(1, .ok))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
    }

    func testThrowingSourceIsContainedAndRegistersWithEmptyToken() throws {
        // 源实现约定"只报告一次、不抛"，但防御性兜底：抛了的源不能让
        // registration future 永不完成，也不能发出两帧。
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(
            userId: "user-1",
            pushToken: TokenSource { _ in throw RegistrarBoom() })
        XCTAssertEqual("", try sentRequest(transport).fcmToken)
        XCTAssertEqual(1, transport.sent.count)
        transport.deliverWsMessage(try registerResponse(1, .ok))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
    }

    func testDoubleReportingSourceSendsExactlyOneFrame() throws {
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(
            userId: "user-1",
            pushToken: TokenSource { onResult in
                onResult("first-token")
                onResult("second-token") // buggy source: reports twice
            })
        XCTAssertEqual("first-token", try sentRequest(transport).fcmToken)
        transport.deliverWsMessage(try registerResponse(1, .ok))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
        XCTAssertEqual(1, transport.sent.count)
    }

    // ---- response surface -----------------------------------------------------

    func testServerErrorCodePropagates() throws {
        let (registrar, transport) = try connectedRegistrar()
        let future = registrar.register(userId: "user-1")
        transport.deliverWsMessage(try registerResponse(1, .targetOffline))
        XCTAssertEqual(Chirp_Common_ErrorCode.targetOffline, try future.get())
    }

    func testClosedConnectionFailsWithRequestError() throws {
        let connection = ChatConnection(
            url: "ws://test",
            transportFactory: { _ in FakeWsTransport() },
            scheduler: scheduler
        )
        let registrar = DeviceRegistrar(conn: connection, deviceId: { "device-1" })
        do {
            _ = try registrar.register(userId: "user-1").get()
            XCTFail("expected failure")
        } catch {
            let requestError = error as? RequestError
            XCTAssertEqual(RequestError.Kind.closed, requestError?.kind)
            XCTAssertFalse((requestError?.message ?? "").isEmpty)
        }
    }
}
