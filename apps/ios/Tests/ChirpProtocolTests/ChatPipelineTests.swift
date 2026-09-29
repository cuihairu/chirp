import ChirpProtos
import SwiftProtobuf
import XCTest
@testable import ChirpProtocol

// ---- test doubles ------------------------------------------------------------

final class StaticProvider: AuthProvider {
    let token: String
    init(token: String) { self.token = token }
    func getToken() -> String { token }
}

final class RenewingProvider: AuthProvider {
    var renewals = 0
    var outcomes: [Chirp_Common_ErrorCode] = []
    func getToken() -> String { "stale-token" }
    func renewToken() -> String? {
        renewals += 1
        return "fresh-token"
    }
    func onAuthResult(_ code: Chirp_Common_ErrorCode, userId: String) throws {
        outcomes.append(code)
    }
}

final class EchoCommand: CommandHandler {
    let name = "echo"
    private(set) var calls: [String] = []
    private let returnValue: Bool
    private let explodes: Bool
    /// When set, this marker is recorded instead of "args:senderId".
    private let marker: String?
    init(returnValue: Bool = true, explodes: Bool = false, marker: String? = nil) {
        self.returnValue = returnValue
        self.explodes = explodes
        self.marker = marker
    }
    func execute(args: String, senderId: String) throws -> Bool {
        calls.append(marker ?? "\(args):\(senderId)")
        if explodes { throw PipelineBoom() }
        return returnValue
    }
}

struct PipelineBoom: Error {}

final class RewriteInterceptor: MessageInterceptor {
    private(set) var afterSend: [String] = []
    func onBeforeSend(
        _ request: Chirp_Chat_SendMessageRequest
    ) throws -> Chirp_Chat_SendMessageRequest? {
        var rewritten = request
        rewritten.content = Data("rewritten".utf8)
        return rewritten
    }
    func onAfterSend(_ request: Chirp_Chat_SendMessageRequest) throws {
        afterSend.append(String(decoding: request.content, as: UTF8.self))
    }
}

final class BlockingInterceptor: MessageInterceptor {
    func onBeforeSend(
        _ request: Chirp_Chat_SendMessageRequest
    ) throws -> Chirp_Chat_SendMessageRequest? {
        throw PipelineBoom()
    }
}

final class DropReceiveInterceptor: MessageInterceptor {
    private(set) var afterCount = 0
    func onBeforeReceive(_ message: Chirp_Chat_ChatMessage) throws -> Chirp_Chat_ChatMessage? {
        String(decoding: message.content, as: UTF8.self) == "drop" ? nil : message
    }
    func onAfterReceive(_ message: Chirp_Chat_ChatMessage) throws {
        afterCount += 1
    }
}

final class FailingStore: MessageStore {
    func save(_ message: Chirp_Chat_ChatMessage) throws {
        throw PipelineBoom()
    }
    func load(
        channelType: Chirp_Chat_ChannelType,
        channelId: String,
        limit: Int,
        beforeTimestamp: Int64
    ) -> [Chirp_Chat_ChatMessage] { [] }
}

final class MessageCollector: ChatEventListener {
    private(set) var seen: [String] = []
    private let explodes: Bool
    init(explodes: Bool = false) { self.explodes = explodes }
    func onMessageReceived(_ message: Chirp_Chat_ChatMessage) throws {
        if explodes { throw PipelineBoom() }
        seen.append(String(decoding: message.content, as: UTF8.self))
    }
}

final class LoginResultCollector: ChatEventListener {
    private(set) var codes: [Chirp_Common_ErrorCode] = []
    func onLoginResult(_ code: Chirp_Common_ErrorCode, userId: String) throws {
        codes.append(code)
    }
}

final class LoginDevicesCollector: ChatEventListener {
    private(set) var devices: [[Chirp_Auth_DevicePresence]] = []
    func onLoginDevices(_ devices: [Chirp_Auth_DevicePresence]) throws {
        self.devices.append(devices)
    }
}

final class KickedCollector: ChatEventListener {
    private(set) var reasons: [String] = []
    func onKicked(reason: String) throws {
        reasons.append(reason)
    }
}

final class PresenceCollector: ChatEventListener {
    private(set) var counts: [Int] = []
    func onDevicesPresence(_ devices: [Chirp_Auth_DevicePresence]) throws {
        counts.append(devices.count)
    }
}

/// Same vector group as Kotlin ChatPipelineTest (23 tests), which mirrors the
/// dart chat_pipeline_test group — three-platform conformance.
final class ChatPipelineTests: XCTestCase {
    private let scheduler = ManualScheduler(startMs: 1_000)
    private var transports: [FakeWsTransport] = []

    override func setUp() {
        super.setUp()
        transports = []
    }

    private func connectedPipeline(selfId: String = "self") throws -> (ChatPipeline, FakeWsTransport) {
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
        let pipeline = ChatPipeline(
            conn: connection,
            selfId: { selfId },
            deviceId: { "device-1" }
        )
        pipeline.start()
        return (pipeline, transports.last!)
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

    private func textMessage(
        _ senderId: String,
        _ content: String,
        channelType: Chirp_Chat_ChannelType = .`private`,
        channelId: String = "a|b",
        timestamp: Int64 = 5_000
    ) -> Chirp_Chat_ChatMessage {
        var m = Chirp_Chat_ChatMessage()
        m.senderID = senderId
        m.channelType = channelType
        m.channelID = channelId
        m.msgType = .text
        m.content = Data(content.utf8)
        m.timestamp = timestamp
        return m
    }

    private func loginResponse(_ code: Chirp_Common_ErrorCode) -> Chirp_Auth_LoginResponse {
        var r = Chirp_Auth_LoginResponse()
        r.code = code
        return r
    }

    private func sentPacket(_ transport: FakeWsTransport) throws -> Chirp_Gateway_Packet {
        try Chirp_Gateway_Packet(serializedBytes: Data(transport.lastSentPayload()))
    }

    private func assertSendError(
        _ future: Promise<Chirp_Chat_SendMessageResponse>,
        file: StaticString = #filePath,
        line: UInt = #line
    ) -> Error {
        do {
            _ = try future.get()
            XCTFail("expected failure", file: file, line: line)
            return RequestError(.closed)
        } catch {
            return error
        }
    }

    // ---- login ---------------------------------------------------------------

    func testLoginUsesExplicitTokenOverProviderOverUserId() throws {
        let (pipeline, transport) = try connectedPipeline()
        pipeline.provider = StaticProvider(token: "provider-token")
        let future = pipeline.login(userId: "user-1", token: "explicit-token")
        let sent = try sentPacket(transport)
        XCTAssertEqual("explicit-token", try Chirp_Auth_LoginRequest(serializedBytes: sent.body).token)
        transport.deliverWsMessage(try wsMessage(.loginResp, sent.sequence, loginResponse(.ok)))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
    }

    func testLoginFallsBackToProviderTokenThenUserId() throws {
        let (pipeline, transport) = try connectedPipeline()
        pipeline.provider = StaticProvider(token: "provider-token")
        let first = pipeline.login(userId: "user-1")
        var sent = try sentPacket(transport)
        XCTAssertEqual("provider-token", try Chirp_Auth_LoginRequest(serializedBytes: sent.body).token)
        transport.deliverWsMessage(try wsMessage(.loginResp, sent.sequence, loginResponse(.ok)))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try first.get())

        let (pipeline2, transport2) = try connectedPipeline()
        let second = pipeline2.login(userId: "user-2")
        sent = try sentPacket(transport2)
        let request = try Chirp_Auth_LoginRequest(serializedBytes: sent.body)
        XCTAssertEqual("user-2", request.token)
        XCTAssertEqual("device-1", request.deviceID)
        transport2.deliverWsMessage(try wsMessage(.loginResp, sent.sequence, loginResponse(.ok)))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try second.get())
    }

    func testAuthFailedGivesTheProviderOneRenewalChance() throws {
        let (pipeline, transport) = try connectedPipeline()
        let provider = RenewingProvider()
        pipeline.provider = provider
        // Answer both login rounds: first AUTH_FAILED, then OK.
        let first = pipeline.login(userId: "user-1")
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, loginResponse(.authFailed)))
        // Second round goes out with the renewed token.
        let second = try sentPacket(transport)
        XCTAssertEqual("fresh-token", try Chirp_Auth_LoginRequest(serializedBytes: second.body).token)
        transport.deliverWsMessage(try wsMessage(.loginResp, 2, loginResponse(.ok)))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try first.get())
        XCTAssertEqual(1, provider.renewals)
        XCTAssertEqual([Chirp_Common_ErrorCode.ok], provider.outcomes)
    }

    func testAuthFailedWithoutProviderOrRenewalIsTerminal() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = LoginResultCollector()
        _ = pipeline.addListener(collector)
        let future = pipeline.login(userId: "user-1")
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, loginResponse(.authFailed)))
        XCTAssertEqual(Chirp_Common_ErrorCode.authFailed, try future.get())
        XCTAssertEqual([Chirp_Common_ErrorCode.authFailed], collector.codes)
    }

    func testSuccessfulLoginResetsBackoffAndFansOutLoginDevices() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = LoginDevicesCollector()
        _ = pipeline.addListener(collector)
        var initial = Chirp_Auth_DevicePresence()
        initial.deviceID = "other-device"
        var response = loginResponse(.ok)
        response.onlineDevices = [initial]
        let future = pipeline.login(userId: "user-1")
        transport.deliverWsMessage(try wsMessage(.loginResp, 1, response))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get())
        XCTAssertEqual(1, collector.devices.count)
        XCTAssertEqual("other-device", collector.devices.first?.first?.deviceID)
    }

    // ---- send ---------------------------------------------------------------

    func testClosedStateIsCheckedBeforeArgumentValidation() throws {
        // Never connected.
        let connection = ChatConnection(
            url: "ws://test",
            transportFactory: { _ in FakeWsTransport() },
            scheduler: scheduler
        )
        let pipeline = ChatPipeline(conn: connection, selfId: { "self" }, deviceId: { "dev" })
        let error = assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`), content: ""))
        let requestError = error as? RequestError
        XCTAssertEqual(RequestError.Kind.closed, requestError?.kind)
    }

    func testEmptyContentAndMissingReceiverAreArgumentErrors() throws {
        let (pipeline, _) = try connectedPipeline()
        XCTAssertTrue(assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: ""
        )) is ChirpArgumentError)
        XCTAssertTrue(assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`), content: "hi"
        )) is ChirpArgumentError)
        XCTAssertTrue(assertSendError(pipeline.send(
            options: SendOptions(channelType: .world), content: "hi"
        )) is ChirpArgumentError)
    }

    func testPrivateSendDerivesSortedPairChannelId() throws {
        let (pipeline, transport) = try connectedPipeline(selfId: "bob")
        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "alice"), content: "hi")
        let sent = try sentPacket(transport)
        let request = try Chirp_Chat_SendMessageRequest(serializedBytes: sent.body)
        XCTAssertEqual("alice|bob", request.channelID)
        XCTAssertEqual("bob", request.senderID)
        XCTAssertEqual("alice", request.receiverID)
        XCTAssertEqual("hi", String(decoding: request.content, as: UTF8.self))
        // Complete the round so the future settles.
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get().code)
    }

    func testSlashCommandWithHandlerIsConsumedLocally() throws {
        let (pipeline, transport) = try connectedPipeline()
        let echo = EchoCommand()
        _ = pipeline.registerCommand(echo)
        let error = assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "/echo hi"))
        XCTAssertEqual(RequestError.Kind.blocked, (error as? RequestError)?.kind)
        XCTAssertEqual(["hi:self"], echo.calls)
        // Nothing reached the wire: no frame was ever sent.
        XCTAssertEqual(0, transport.sent.count)
    }

    func testUnknownSlashCommandIsBlockedNotSent() throws {
        let (pipeline, _) = try connectedPipeline()
        _ = pipeline.registerCommand(EchoCommand())
        let error = assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "/nope"))
        let requestError = error as? RequestError
        XCTAssertEqual(RequestError.Kind.blocked, requestError?.kind)
        XCTAssertTrue(requestError?.message?.contains("unknown command") ?? false)
    }

    func testSlashCommandWithNoHandlersPassesThrough() throws {
        let (pipeline, transport) = try connectedPipeline()
        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "/echo hi")
        let sent = try sentPacket(transport)
        XCTAssertEqual(Chirp_Gateway_MsgID.sendMessageReq, sent.msgID)
        XCTAssertEqual(
            "/echo hi",
            String(decoding: try Chirp_Chat_SendMessageRequest(serializedBytes: sent.body).content, as: UTF8.self))
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        XCTAssertEqual(Chirp_Common_ErrorCode.ok, try future.get().code)
    }

    func testThrowingCommandHandlerDeclinesForTheNextSameNameHandler() throws {
        let (pipeline, _) = try connectedPipeline()
        let first = EchoCommand(explodes: true, marker: "first")
        let second = EchoCommand(marker: "second")
        _ = pipeline.registerCommand(first)
        _ = pipeline.registerCommand(second)
        let error = assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "/echo x"))
        XCTAssertTrue(error is RequestError)
        XCTAssertEqual(first.calls + second.calls, ["first", "second"])
    }

    func testInterceptorRewriteReachesTheWireAndAfterSendFires() throws {
        let (pipeline, transport) = try connectedPipeline()
        let interceptor = RewriteInterceptor()
        pipeline.interceptor = interceptor
        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "original")
        let sent = try sentPacket(transport)
        XCTAssertEqual(
            "rewritten",
            String(decoding: try Chirp_Chat_SendMessageRequest(serializedBytes: sent.body).content, as: UTF8.self))
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        _ = try future.get()
        XCTAssertEqual(["rewritten"], interceptor.afterSend)
    }

    func testInterceptorsThatThrowAreBlocking() throws {
        let (pipeline, transport) = try connectedPipeline()
        pipeline.interceptor = BlockingInterceptor()
        let error = assertSendError(pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "hi"))
        XCTAssertEqual(RequestError.Kind.blocked, (error as? RequestError)?.kind)
        XCTAssertEqual(0, transport.sent.count)
    }

    func testArchiveSavesBothDirectionsAndForwardCallsPassThrough() throws {
        let (pipeline, transport) = try connectedPipeline()
        let store = MemoryMessageStore()
        pipeline.store = store

        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "alice"), content: "out")
        let sent = try sentPacket(transport)
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        _ = try future.get()
        // Sent copy archived (empty messageId is the sent-direction marker).
        XCTAssertEqual(
            1, store.load(channelType: .`private`, channelId: "alice|self", limit: 10).count)

        // Incoming push archived and fanned out (same channel bucket).
        let incoming = textMessage("alice", "in", channelId: "alice|self")
        transport.deliverWsMessage(try wsMessage(.chatMessageNotify, 0, incoming))
        XCTAssertEqual(
            2, store.load(channelType: .`private`, channelId: "alice|self", limit: 10).count)
        XCTAssertEqual(
            "out",
            contents(store.load(channelType: .`private`, channelId: "alice|self", limit: 10))[1])
        XCTAssertEqual(
            "in",
            contents(store.load(channelType: .`private`, channelId: "alice|self", limit: 10))[0])
    }

    // ---- incoming path --------------------------------------------------------

    private func contents(_ page: [Chirp_Chat_ChatMessage]) -> [String] {
        page.map { String(decoding: $0.content, as: UTF8.self) }
    }

    func testInterceptorReceiveDropKillsArchiveListenersAndAfterReceive() throws {
        let (pipeline, transport) = try connectedPipeline()
        let store = MemoryMessageStore()
        pipeline.store = store
        let interceptor = DropReceiveInterceptor()
        pipeline.interceptor = interceptor
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "drop")))
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "keep")))
        XCTAssertEqual(["keep"], collector.seen)
        XCTAssertEqual(1, interceptor.afterCount)
        // The dropped message never reached the archive either.
        XCTAssertEqual(
            1, store.load(channelType: .`private`, channelId: "a|b", limit: 10).count)
    }

    func testArchiveFailureDoesNotKillTheFanOut() throws {
        let (pipeline, transport) = try connectedPipeline()
        pipeline.store = FailingStore()
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "hello")))
        XCTAssertEqual(["hello"], collector.seen)
    }

    func testThrowingListenersDoNotStarveEachOther() throws {
        let (pipeline, transport) = try connectedPipeline()
        _ = pipeline.addListener(MessageCollector(explodes: true))
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "hello")))
        XCTAssertEqual(["hello"], collector.seen)
    }

    func testUndecodableIncomingBodyIsIgnoredAndTheLinkSurvives() throws {
        let (pipeline, transport) = try connectedPipeline()
        let store = MemoryMessageStore()
        pipeline.store = store
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        // Valid framing, undecodable payload: 0x7F is an invalid wire type.
        var packet = Chirp_Gateway_Packet()
        packet.msgID = .chatMessageNotify
        packet.sequence = 0
        packet.body = Data([0x7F])
        transport.deliverWsMessage(try encodeFrame(payload: [UInt8](packet.serializedData())))
        XCTAssertEqual(0, collector.seen.count)
        XCTAssertEqual(
            0, store.load(channelType: .`private`, channelId: "a|b", limit: 10).count)
        // The link is still alive: a well-formed push gets through.
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "ok")))
        XCTAssertEqual(["ok"], collector.seen)
    }

    func testKickFansOutTheReason() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = KickedCollector()
        _ = pipeline.addListener(collector)
        var kick = Chirp_Auth_KickNotify()
        kick.reason = "dup login"
        transport.deliverWsMessage(try wsMessage(.kickNotify, 0, kick))
        XCTAssertEqual(["dup login"], collector.reasons)
    }

    func testMalformedKickStillFansOutWithAnEmptyReason() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = KickedCollector()
        _ = pipeline.addListener(collector)
        // A body that is not a KickNotify still proves the session is dead;
        // unknown fields are skipped and the reason defaults to empty.
        transport.deliverWsMessage(
            try wsMessage(.kickNotify, 0, textMessage("x", "not a kick")))
        XCTAssertEqual([""], collector.reasons)
    }

    func testDevicesPresenceEmptyListIsSkippedAndMalformedDropped() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = PresenceCollector()
        _ = pipeline.addListener(collector)
        var d1 = Chirp_Auth_DevicePresence()
        d1.deviceID = "d1"
        var d2 = Chirp_Auth_DevicePresence()
        d2.deviceID = "d2"
        var notify = Chirp_Auth_DevicesPresenceNotify()
        notify.devices = [d1, d2]
        transport.deliverWsMessage(try wsMessage(.devicesPresenceNotify, 0, notify))
        // Empty list: skipped silently.
        transport.deliverWsMessage(
            try wsMessage(.devicesPresenceNotify, 0, Chirp_Auth_DevicesPresenceNotify()))
        // Malformed (not a device list): dropped.
        transport.deliverWsMessage(
            try wsMessage(.devicesPresenceNotify, 0, textMessage("x", "nope")))
        XCTAssertEqual([2], collector.counts)
    }

    func testStopUnsubscribesIncoming() throws {
        let (pipeline, transport) = try connectedPipeline()
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        pipeline.stop()
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "after stop")))
        XCTAssertEqual(0, collector.seen.count)
        // Idempotent restart rewires.
        pipeline.start()
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("alice", "after restart")))
        XCTAssertEqual(["after restart"], collector.seen)
    }

    // ---- 收尾对拍补齐（2026-09-29，Flutter 移除批次）------------------------
    // dart chat_pipeline_test 里语义已随移植落地、但两平台此前都没有专门
    // 向量的四条：custom msgType 上线、状态翻转扇出、重连事件扇出、无
    // store 降级。Flutter 应用删除后这组向量由原生包独占承载。

    func testCustomMsgTypeOnSendReachesTheWire() throws {
        let (pipeline, transport) = try connectedPipeline()
        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer", msgType: .image),
            content: "hi")
        let sent = try sentPacket(transport)
        XCTAssertEqual(
            Chirp_Chat_MsgType.image,
            try Chirp_Chat_SendMessageRequest(serializedBytes: sent.body).msgType)
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        _ = try future.get()
    }

    func testConnectionStateFlipsFanOutToListeners() throws {
        let connection = ChatConnection(
            url: "ws://test/chirp",
            transportFactory: { [self] _ in
                let fake = FakeWsTransport()
                transports.append(fake)
                return fake
            },
            random: FakeRandom(),
            scheduler: scheduler
        )
        _ = try connection.connect().get()
        let pipeline = ChatPipeline(
            conn: connection,
            selfId: { "self" },
            deviceId: { "device-1" }
        )
        final class StateCollector: ChatEventListener {
            private(set) var states: [ConnState] = []
            func onConnectionStateChanged(_ state: ConnState) throws {
                states.append(state)
            }
        }
        let collector = StateCollector()
        _ = pipeline.addListener(collector)
        pipeline.start()

        transports[0].closedHandler!() // remote drop without our close()
        XCTAssertEqual(.waitingReconnect, collector.states.last)
        scheduler.advance(500)
        XCTAssertEqual(.connected, collector.states.last)
    }

    func testReconnectingAndReconnectedFanOutThroughThePipeline() throws {
        // FakeRandom 的抖动取中点，退避延迟恰等于基数——梯子可精确断言
        // （同 ChatConnectionTests 的同名驱动）。
        let connection = ChatConnection(
            url: "ws://test/chirp",
            transportFactory: { [self] _ in
                let fake = FakeWsTransport()
                transports.append(fake)
                return fake
            },
            random: FakeRandom(),
            scheduler: scheduler
        )
        _ = try connection.connect().get()
        let pipeline = ChatPipeline(
            conn: connection,
            selfId: { "self" },
            deviceId: { "device-1" }
        )
        final class ReconnectCollector: ChatEventListener {
            private(set) var events: [(Int, Int64)] = []
            private(set) var reconnected = 0
            func onReconnecting(attempt: Int, delayMs: Int64) throws {
                events.append((attempt, delayMs))
            }
            func onReconnected() throws { reconnected += 1 }
        }
        let collector = ReconnectCollector()
        _ = pipeline.addListener(collector)
        pipeline.start()

        transports[0].closedHandler!()
        // (Swift tuples carry no Equatable — assert the two columns.)
        XCTAssertEqual(collector.events.map(\.0), [1])
        XCTAssertEqual(collector.events.map(\.1), [500])
        XCTAssertEqual(0, collector.reconnected)
        scheduler.advance(500)
        XCTAssertEqual(1, collector.reconnected)
        transports[1].closedHandler!()
        XCTAssertEqual(collector.events.map(\.0), [1, 2])
        XCTAssertEqual(collector.events.map(\.1), [500, 1_000])
        scheduler.advance(1_000)
        XCTAssertEqual(2, collector.reconnected)
    }

    func testWorksWithoutAStoreSendReceiveAndQueriesDegrade() throws {
        let (pipeline, transport) = try connectedPipeline()
        // store 保持 nil（默认）：发送/接收照常，查询面降级不崩。
        let collector = MessageCollector()
        _ = pipeline.addListener(collector)
        let future = pipeline.send(
            options: SendOptions(channelType: .`private`, receiverId: "peer"), content: "hi")
        let sent = try sentPacket(transport)
        var response = Chirp_Chat_SendMessageResponse()
        response.code = .ok
        transport.deliverWsMessage(try wsMessage(.sendMessageResp, sent.sequence, response))
        _ = try future.get()
        transport.deliverWsMessage(
            try wsMessage(.chatMessageNotify, 0, textMessage("peer", "yo", channelId: "peer|self")))
        XCTAssertEqual(["yo"], collector.seen)

        XCTAssertEqual([], pipeline.loadHistory(channelType: .`private`, channelId: "peer|self", limit: 10))
        pipeline.markRead(channelType: .`private`, channelId: "peer|self")
        XCTAssertEqual(0, pipeline.unreadCount(channelType: .`private`, channelId: "peer|self"))
        pipeline.cleanup(olderThanMs: 60_000)
    }
}
