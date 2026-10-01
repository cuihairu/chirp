import XCTest
import ChirpProtos
import SwiftProtobuf
@testable import ChirpProtocol

/// 词库下发同步（WordFilterSync）的协议面测试：真实 ChatConnection +
/// FakeWsTransport，按线格式应答 FETCH_RESP / 投递 UPDATE_NOTIFY。
final class WordFilterSyncTests: XCTestCase {
    private let scheduler = ManualScheduler(startMs: 1_000)
    private var transports: [FakeWsTransport] = []

    override func setUp() {
        super.setUp()
        transports = []
    }

    private func connected() throws -> (ChatConnection, FakeWsTransport) {
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
        return (connection, transports.last!)
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

    private func lexicon(
        _ version: Int64,
        _ enabled: Bool,
        _ policy: Chirp_Chat_WordFilterDeliveryPolicy,
        _ text: String
    ) -> Chirp_Chat_WordFilterLexicon {
        var lex = Chirp_Chat_WordFilterLexicon()
        lex.version = version
        lex.enabled = enabled
        lex.policy = policy
        lex.replacement = "**"
        lex.lexicon = text
        return lex
    }

    private func fetchResponse(
        _ sequence: Int64,
        code: Chirp_Common_ErrorCode,
        lex: Chirp_Chat_WordFilterLexicon?
    ) throws -> [UInt8] {
        var resp = Chirp_Chat_WordFilterFetchResponse()
        resp.code = code
        if let lex { resp.lexicon = lex }
        return try wsMessage(.wordFilterFetchResp, sequence, resp)
    }

    private struct Verdict: Equatable {
        let allowed: Bool
        let content: String
    }

    /// Runs the sync's pre-check over one send.
    private func run(_ sync: WordFilterSync, _ content: String) throws -> Verdict {
        var request = Chirp_Chat_SendMessageRequest()
        request.content = Data(content.utf8)
        guard let effective = try sync.onBeforeSend(request) else {
            return Verdict(allowed: false, content: content) // nil = 拦截（内容不上线）
        }
        return Verdict(allowed: true, content: String(decoding: effective.content, as: UTF8.self))
    }

    private func sentRequest(_ transport: FakeWsTransport) throws -> Chirp_Chat_WordFilterFetchRequest {
        let packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        return try Chirp_Chat_WordFilterFetchRequest(serializedBytes: packet.body)
    }

    // ---- fetch -------------------------------------------------------------

    func testFetchSendsKnownVersionAndAppliesTheServerLexicon() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        let future = sync.fetch()
        let sent = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        XCTAssertEqual(sent.msgID, .wordFilterFetchReq)
        XCTAssertEqual(try sentRequest(transport).knownVersion, Int64(0))
        transport.deliverWsMessage(try fetchResponse(
            sent.sequence,
            code: .ok,
            lex: lexicon(3, true, .wordFilterPolicyReplace, "spam\nbad\n")))
        XCTAssertTrue(try future.get())
        XCTAssertEqual(sync.currentVersion, 3)
        XCTAssertEqual(sync.wordCount, 2)
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: true, content: "hello **"))
    }

    func testConditionalGetSkipsTheRebuildWhenTheServerVersionMatches() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        var future = sync.fetch()
        var packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        transport.deliverWsMessage(try fetchResponse(
            packet.sequence, code: .ok,
            lex: lexicon(3, true, .wordFilterPolicyReplace, "spam")))
        XCTAssertTrue(try future.get())

        // 第二轮：同版本应答不带文本（条件 GET 命中），预检不动。
        future = sync.fetch()
        packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        XCTAssertEqual(try sentRequest(transport).knownVersion, Int64(3))
        transport.deliverWsMessage(try fetchResponse(
            packet.sequence, code: .ok,
            lex: lexicon(3, true, .wordFilterPolicyReplace, "")))
        XCTAssertTrue(try future.get())
        XCTAssertEqual(sync.currentVersion, 3)
        XCTAssertEqual(sync.wordCount, 1)
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: true, content: "hello **"))
    }

    func testNonOkFetchCodeResolvesFalseAndAppliesNothing() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        let future = sync.fetch()
        let packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        transport.deliverWsMessage(try fetchResponse(packet.sequence, code: .authFailed, lex: nil))
        XCTAssertFalse(try future.get())
        XCTAssertEqual(sync.currentVersion, 0)
        XCTAssertEqual(sync.wordCount, 0)
    }

    func testFetchTimeoutResolvesFalseWithoutThrowing() throws {
        let (connection, _) = try connected()
        let sync = WordFilterSync(conn: connection, timeoutMs: 5_000)
        let future = sync.fetch()
        scheduler.advance(5_000)
        XCTAssertFalse(try future.get())
    }

    // ---- UPDATE_NOTIFY -----------------------------------------------------

    func testNotifyHotSwapsThePrecheck() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.start()
        var notify = Chirp_Chat_WordFilterUpdateNotify()
        notify.lexicon = lexicon(4, true, .wordFilterPolicyReplace, "evil")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, notify))
        XCTAssertEqual(sync.currentVersion, 4)
        XCTAssertEqual(try run(sync, "you evil one"), Verdict(allowed: true, content: "you ** one"))
    }

    func testStaleNotifiesAreIgnored() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.start()
        var fresh = Chirp_Chat_WordFilterUpdateNotify()
        fresh.lexicon = lexicon(5, true, .wordFilterPolicyReplace, "fresh")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, fresh))
        var stale = Chirp_Chat_WordFilterUpdateNotify()
        stale.lexicon = lexicon(2, true, .wordFilterPolicyReplace, "stale")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, stale))
        XCTAssertEqual(sync.currentVersion, 5)
        XCTAssertEqual(sync.wordCount, 1)
        XCTAssertEqual(try run(sync, "fresh"), Verdict(allowed: true, content: "**"))
        XCTAssertEqual(try run(sync, "stale"), Verdict(allowed: true, content: "stale"))
    }

    func testServerWithoutLexiconClearsEvenTheLocalFallback() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.loadLocal(text: "spam")
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: true, content: "hello **"))
        sync.start()
        var notify = Chirp_Chat_WordFilterUpdateNotify()
        notify.lexicon = lexicon(1, false, .wordFilterPolicyReplace, "")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, notify))
        XCTAssertEqual(sync.currentVersion, 1)
        XCTAssertEqual(sync.wordCount, 0)
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: true, content: "hello spam"))
    }

    func testRecordPolicyPassesThroughWithoutAPrecheck() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.start()
        var notify = Chirp_Chat_WordFilterUpdateNotify()
        notify.lexicon = lexicon(2, true, .wordFilterPolicyRecord, "spam")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, notify))
        XCTAssertEqual(sync.wordCount, 0)
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: true, content: "hello spam"))
    }

    func testStopDetachesTheNotifySubscription() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.start()
        sync.stop()
        var notify = Chirp_Chat_WordFilterUpdateNotify()
        notify.lexicon = lexicon(9, true, .wordFilterPolicyReplace, "late")
        transport.deliverWsMessage(try wsMessage(.wordFilterUpdateNotify, 0, notify))
        XCTAssertEqual(sync.currentVersion, 0)
    }

    // ---- fallback & policy mirror -------------------------------------------

    func testLoadLocalIsTheFallbackUntilTheServerLexiconSupersedesIt() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        sync.loadLocal(text: "localbad", policy: .reject)
        XCTAssertEqual(try run(sync, "x localbad"), Verdict(allowed: false, content: "x localbad"))
        let future = sync.fetch()
        let packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        transport.deliverWsMessage(try fetchResponse(
            packet.sequence, code: .ok,
            lex: lexicon(1, true, .wordFilterPolicyReplace, "serverbad")))
        XCTAssertTrue(try future.get())
        XCTAssertEqual(try run(sync, "x localbad"), Verdict(allowed: true, content: "x localbad"))
        XCTAssertEqual(try run(sync, "x serverbad"), Verdict(allowed: true, content: "x **"))
    }

    func testRejectPolicyBlocksTheSendBeforeTheWire() throws {
        let (connection, transport) = try connected()
        let sync = WordFilterSync(conn: connection)
        let future = sync.fetch()
        let packet = try Chirp_Gateway_Packet(serializedBytes: transport.lastSentPayload())
        transport.deliverWsMessage(try fetchResponse(
            packet.sequence, code: .ok,
            lex: lexicon(1, true, .wordFilterPolicyReject, "spam")))
        XCTAssertTrue(try future.get())
        XCTAssertEqual(try run(sync, "hello spam"), Verdict(allowed: false, content: "hello spam"))
        XCTAssertEqual(try run(sync, "clean"), Verdict(allowed: true, content: "clean"))
    }
}
