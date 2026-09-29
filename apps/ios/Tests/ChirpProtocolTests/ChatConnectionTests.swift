import XCTest
import ChirpProtos
import SwiftProtobuf
@testable import ChirpProtocol

/// Same vector group as the Kotlin ChatConnectionTest (16 tests), which in
/// turn mirrors the dart chirp_client_test group — three-platform conformance.
final class ChatConnectionTests: XCTestCase {
    private let scheduler = ManualScheduler(startMs: 1_000)
    private var transports: [FakeWsTransport] = []

    /// One-shot script: the next transport created by the factory fails open with this.
    private var nextFailOpen: Error?

    override func setUp() {
        super.setUp()
        transports = []
        nextFailOpen = nil
    }

    /// Fresh connection on a scripted fake; auto-opens like a happy path.
    private func connection(
        options: ChirpClientOptions = ChirpClientOptions(),
        random: RandomSource = FakeRandom()
    ) -> ChatConnection {
        ChatConnection(
            url: "ws://test/chirp",
            transportFactory: { [self] _ in
                let fake = FakeWsTransport()
                fake.failOpenWith = nextFailOpen
                nextFailOpen = nil
                transports.append(fake)
                return fake
            },
            options: options,
            random: random,
            scheduler: scheduler
        )
    }

    @discardableResult
    private func connect(_ connection: ChatConnection) throws -> FakeWsTransport {
        _ = try connection.connect().get()
        return transports.last!
    }

    /// Inbound server Packet as a full WebSocket message (framed).
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

    private func loginRequest(token: String = "", deviceId: String = "", platform: String = "") -> Chirp_Auth_LoginRequest {
        var r = Chirp_Auth_LoginRequest()
        r.token = token
        r.deviceID = deviceId
        r.platform = platform
        return r
    }

    // ---- request/response ---------------------------------------------------

    func testLoginRoundTripCorrelatesBySequenceAndDecodesTheTypedResponse() throws {
        let connection = connection()
        let transport = try connect(connection)
        XCTAssertEqual(connection.state, .connected)

        let request = loginRequest(token: "t-1", deviceId: "dev-1", platform: "ios")
        let future = connection.request(spec: MsgSpecs.login, body: request)

        // The outgoing frame carries Packet{LOGIN_REQ, seq=1, body}.
        let sent = try Chirp_Gateway_Packet(serializedBytes: Data(transport.lastSentPayload()))
        XCTAssertEqual(sent.msgID, .loginReq)
        XCTAssertEqual(sent.sequence, 1)
        XCTAssertEqual(try Chirp_Auth_LoginRequest(serializedBytes: sent.body).token, "t-1")

        // Server answers with the RESP msgId and the echoed sequence.
        var response0 = Chirp_Auth_LoginResponse()
        response0.sessionID = "s-9"
        response0.userID = "u-1"
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, response0))
        let response = try future.get()
        XCTAssertEqual(response.code, .ok)
        XCTAssertEqual(response.sessionID, "s-9")
    }

    func testErrorResponsesStillCompleteSoCallersInspectTheCode() throws {
        let connection = connection()
        let transport = try connect(connection)
        let future = connection.request(spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest())
        var failed = Chirp_Auth_LoginResponse()
        failed.code = .authFailed
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, failed))
        XCTAssertEqual(try future.get().code, .authFailed)
    }

    func testRequestBeforeConnectFailsFastWithoutTouchingTheTransport() {
        let connection = connection()
        XCTAssertThrowsError(try connection.request(
            spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest()
        ).get()) { error in
            let requestError = error as? RequestError
            XCTAssertEqual(requestError?.kind, .closed)
        }
        XCTAssertEqual(transports.count, 0)
    }

    func testRequestTimesOutWhenNoResponseArrivesAndLateResponsesAreIgnored() throws {
        let connection = connection()
        let transport = try connect(connection)
        let future = connection.request(
            spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest(), timeoutMs: 1_000)
        scheduler.advance(1_000)
        XCTAssertThrowsError(try future.get()) { error in
            XCTAssertEqual((error as? RequestError)?.kind, .timeout)
        }

        // A response to an already timed-out request must not throw.
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, Chirp_Auth_LoginResponse()))
    }

    // ---- heartbeat ----------------------------------------------------------

    func testHeartbeatPingConsumesSequenceAndCarriesTheVirtualClockTimestamp() throws {
        let connection = connection()
        let transport = try connect(connection)
        scheduler.advance(25_000)
        let sent = try Chirp_Gateway_Packet(serializedBytes: Data(transport.lastSentPayload()))
        XCTAssertEqual(sent.msgID, .heartbeatPing)
        XCTAssertEqual(sent.sequence, 1)
        // The tick fires at virtual 26000 (start 1000 + interval).
        XCTAssertEqual(try Chirp_Gateway_HeartbeatPing(serializedBytes: sent.body).timestamp, 26_000)
    }

    func testPongResetsMissedPongsAndRecordsClockOffset() throws {
        let connection = connection()
        let transport = try connect(connection)
        // Two ping ticks without any pong exhaust maxMissedPongs=2; the third
        // would kill the link. A pong between ticks resets the counter.
        scheduler.advance(25_000)
        scheduler.advance(25_000)
        var pong = Chirp_Gateway_HeartbeatPong()
        pong.serverTime = 1_400
        transport.deliverWsMessage(try wsMessage(.heartbeatPong, 2, pong))
        // Two ticks moved the virtual clock 1000 → 51000.
        XCTAssertEqual(connection.clockOffsetMs, 1_400 - 51_000)
        // Survives the third tick that would have closed a pong-less link.
        scheduler.advance(25_000)
        XCTAssertEqual(transport.closeCount, 0)
        XCTAssertGreaterThanOrEqual(transport.sent.count, 3)
    }

    func testMissedPongsBeyondTheLimitCloseTheLinkIntoReconnect() throws {
        let connection = connection()
        let transport = try connect(connection)
        scheduler.advance(25_000 * 3) // maxMissedPongs=2: the 3rd tick kills the link
        XCTAssertEqual(transport.closeCount, 1)
        XCTAssertEqual(connection.state, .waitingReconnect)
        XCTAssertGreaterThan(scheduler.pendingTasks(), 0) // backoff timer armed
    }

    // ---- reconnect backoff --------------------------------------------------

    func testAbnormalCloseSchedulesBackoffReconnectAndSuccessFiresReconnected() throws {
        // FakeRandom returns the jitter midpoint, so the scheduled delay
        // equals base exactly and the ladder is assertable.
        let connection = connection(random: FakeRandom())
        var reconnecting: [(Int, Int64)] = []
        var reconnected = 0
        _ = connection.onReconnecting { attempt, delay in reconnecting.append((attempt, delay)) }
        _ = connection.onReconnected { reconnected += 1 }

        let transport = try connect(connection)
        transport.closedHandler!() // remote drop without our close()
        XCTAssertEqual(connection.state, .waitingReconnect)
        // FakeRandom(jitter=0): delay == base exactly, attempt 1-based.
        // (Swift tuples carry no Equatable — assert the two columns.)
        XCTAssertEqual(reconnecting.map(\.0), [1])
        XCTAssertEqual(reconnecting.map(\.1), [500])
        XCTAssertEqual(reconnected, 0)

        scheduler.advance(500)
        XCTAssertEqual(transports.count, 2) // the timer reconnected
        XCTAssertEqual(connection.state, .connected)
        XCTAssertEqual(reconnected, 1)

        // The counter keeps climbing across drops until resetBackoff().
        transports[1].closedHandler!()
        XCTAssertEqual(reconnecting.map(\.0), [1, 2])
        XCTAssertEqual(reconnecting.map(\.1), [500, 1_000])
        connection.resetBackoff()
        // Fire the pending timer so the third socket exists, then drop it.
        scheduler.advance(1_000)
        XCTAssertEqual(transports.count, 3)
        transports[2].closedHandler!()
        // After the reset the next drop restarts the ladder at attempt 1.
        XCTAssertEqual(reconnecting.last?.0, 1)
        XCTAssertEqual(reconnecting.last?.1, 500)
    }

    func testFailedReconnectAttemptLoopsBackIntoBackoff() throws {
        let connection = connection()
        _ = try connect(connection)
        transports[0].closedHandler!()
        // Script the transport that the reconnect timer is about to create.
        struct ServerDown: Error {}
        nextFailOpen = ServerDown()
        scheduler.advance(500)
        // The attempt opened, failed, and scheduled the next try (attempt 2).
        XCTAssertEqual(connection.state, .waitingReconnect)
        XCTAssertEqual(transports.count, 2)
        scheduler.advance(1_000)
        XCTAssertEqual(transports.count, 3) // third transport opening now
    }

    // ---- kick / close semantics ----------------------------------------------

    func testKickedIsTerminalForAutoReconnectButExplicitConnectRestarts() throws {
        let connection = connection()
        let transport = try connect(connection)
        var states: [ConnState] = []
        _ = connection.onStateChange { states.append($0) }

        var kick = Chirp_Auth_KickNotify()
        kick.reason = "logged in elsewhere"
        transport.deliverWsMessage(try wsMessage(.kickNotify, 0, kick))

        XCTAssertEqual(connection.state, .kicked)
        XCTAssertTrue(connection.kicked)
        XCTAssertEqual(transport.closeCount, 1)
        XCTAssertTrue(states.contains(.kicked))
        // No reconnect timer may be armed behind a kick.
        XCTAssertEqual(scheduler.pendingTasks(), 0)
        transport.closedHandler!() // the transport still goes down later
        XCTAssertEqual(connection.state, .kicked)
        XCTAssertEqual(scheduler.pendingTasks(), 0)
        // In-flight requests were flushed with the kicked kind.
        XCTAssertThrowsError(try connection.request(
            spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest()
        ).get()) { error in
            XCTAssertEqual((error as? RequestError)?.kind, .closed)
        }

        // An explicit connect() is a fresh user action: clears the kick.
        try connection.connect().get()
        XCTAssertEqual(connection.state, .connected)
        XCTAssertFalse(connection.kicked)
    }

    func testDisconnectCancelsThePendingReconnectTimer() throws {
        let connection = connection()
        let transport = try connect(connection)
        transport.closedHandler!()
        XCTAssertEqual(connection.state, .waitingReconnect)
        connection.disconnect()
        XCTAssertEqual(connection.state, .closed)
        XCTAssertEqual(scheduler.pendingTasks(), 0)
        scheduler.advance(60_000) // the cancelled timer must not resurrect anything
        XCTAssertEqual(transports.count, 1)
        XCTAssertEqual(connection.state, .closed)
    }

    func testKickPendingRequestsAreFlushedWithTheKickedKind() throws {
        let connection = connection()
        let transport = try connect(connection)
        let future = connection.request(spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest())
        transport.deliverWsMessage(try wsMessage(.kickNotify, 0, Chirp_Auth_KickNotify()))
        XCTAssertThrowsError(try future.get()) { error in
            XCTAssertEqual((error as? RequestError)?.kind, .kicked)
        }
    }

    // ---- notify surface -------------------------------------------------------

    func testNotifiesDispatchToRegisteredHandlersUntilUnsubscribed() throws {
        let connection = connection()
        let transport = try connect(connection)
        var seen = 0
        let unsubscribe = connection.onNotify(msgId: .chatMessageNotify) { _ in seen += 1 }

        transport.deliverWsMessage(try wsMessage(.chatMessageNotify, 0, Chirp_Gateway_HeartbeatPing()))
        transport.deliverWsMessage(try wsMessage(.chatMessageNotify, 0, Chirp_Gateway_HeartbeatPing()))
        XCTAssertEqual(seen, 2)
        unsubscribe()
        transport.deliverWsMessage(try wsMessage(.chatMessageNotify, 0, Chirp_Gateway_HeartbeatPing()))
        XCTAssertEqual(seen, 2)
        // Notify dispatch left the link untouched.
        XCTAssertEqual(transport.closeCount, 0)
        XCTAssertNil(connection.clockOffsetMs)
    }

    func testUndecodablePacketsAreIgnoredButCorruptFramingKillsTheLink() throws {
        let connection = connection()
        let transport = try connect(connection)
        // Undecodable packet payload inside a valid frame: ignored.
        transport.deliverWsMessage(try encodeFrame(payload: [0x3F, 0x0A])) // field 7 variant garbage
        XCTAssertEqual(connection.state, .connected)
        XCTAssertEqual(transport.closeCount, 0)
        // Corrupt framing (prefix lies about the length → FrameError): dead
        // link, the close event schedules the reconnect.
        let evil: [UInt8] = [0x7F, 0x00, 0x00, 0x00]
        transport.deliverWsMessage(evil)
        XCTAssertEqual(transport.closeCount, 1)
        XCTAssertEqual(connection.state, .waitingReconnect)
    }

    func testDisconnectThenExplicitConnectOpensAFreshSocket() throws {
        let connection = connection()
        let transport = try connect(connection)
        connection.disconnect()
        XCTAssertEqual(connection.state, .closed)
        XCTAssertGreaterThanOrEqual(transport.closeCount, 1)
        XCTAssertThrowsError(try connection.request(
            spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest()
        ).get())
        // Dart semantics: closed may connect() again.
        _ = try connect(connection)
        XCTAssertEqual(transports.count, 2)
        XCTAssertEqual(connection.state, .connected)
    }

    func testSendFireAndForgetConsumesSequenceAndThrowsWhenNotConnected() throws {
        let connection = connection()
        XCTAssertThrowsError(try connection.send(msgId: .messageAck, body: []))
        let transport = try connect(connection)
        _ = connection.request(spec: MsgSpecs.login, body: Chirp_Auth_LoginRequest())
        try connection.send(msgId: .messageAck, body: [9])
        let sent = try Chirp_Gateway_Packet(serializedBytes: Data(transport.lastSentPayload()))
        XCTAssertEqual(sent.msgID, .messageAck)
        XCTAssertEqual(sent.sequence, 2) // the login request consumed 1
    }
}
