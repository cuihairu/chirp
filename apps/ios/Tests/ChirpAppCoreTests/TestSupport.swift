import ChirpAppCore
import ChirpProtos
import ChirpProtocol
import Foundation
import SwiftProtobuf
import XCTest

/// P2 服务级测试的驱动件(手法对齐 ChirpProtocolTests:FakeWsTransport +
/// 帧编解码 + 按序号回响应)。连接回调线程与测试线程同为调用线程(同步
/// open + 手工 deliver),无需调度控制;心跳/重连定时器不触发即不参与。

/// 记录型假传输:open 即成功;测试从 `sent` 读请求帧、用 `deliver` 注入
/// 服务端帧(与 ChirpProtocolTests.FakeWsTransport 同款,本目标独立成套)。
final class FakeWsTransport: WsTransport {
    private(set) var sent: [[UInt8]] = []
    private(set) var closeCount = 0
    private var binaryHandler: (([UInt8]) -> Void)?
    private var closedHandler: (() -> Void)?

    func open(_ onResult: @escaping (Error?) -> Void) {
        onResult(nil)
    }

    func onBinary(_ handler: @escaping ([UInt8]) -> Void) {
        binaryHandler = handler
    }

    func onClosed(_ handler: @escaping () -> Void) {
        closedHandler = handler
    }

    func send(_ data: [UInt8]) -> Bool {
        sent.append(data)
        return true
    }

    func close() {
        closeCount += 1
        // 与 dart IoWebSocket 同语义:close 恰一次通告下行。
        closedHandler?()
    }

    /// 注入一条服务端 WebSocket 二进制帧(已含长度前缀)。
    func deliver(_ frame: [UInt8]) {
        binaryHandler?(frame)
    }

    /// 模拟链路断开(不经 close():直接通告,不走本地挥手)。
    func dropLink() {
        closedHandler?()
    }

    /// 最后一条出站帧的内层载荷(u32 长度前缀剥掉)。
    func lastSentPayload() -> [UInt8] {
        let frame = sent.last!
        let length =
            Int(frame[0]) << 24 | Int(frame[1]) << 16 | Int(frame[2]) << 8 | Int(frame[3])
        return Array(frame[4..<(4 + length)])
    }
}

/// 服务端帧构造:RESP + 指定序号。
func wsResponse(
    _ msgId: Chirp_Gateway_MsgID, _ sequence: Int64, _ body: SwiftProtobuf.Message
) throws -> [UInt8] {
    var packet = Chirp_Gateway_Packet()
    packet.msgID = msgId
    packet.sequence = sequence
    packet.body = try body.serializedData()
    return try encodeFrame(payload: [UInt8](packet.serializedData()))
}

/// NOTIFY 形态(seq=0,与广播一致)。
func wsNotify(_ msgId: Chirp_Gateway_MsgID, _ body: SwiftProtobuf.Message) throws -> [UInt8] {
    try wsResponse(msgId, 0, body)
}

func sentPacket(_ of: FakeWsTransport) throws -> Chirp_Gateway_Packet {
    try Chirp_Gateway_Packet(serializedBytes: Data(of.lastSentPayload()))
}

/// 最后一条出站请求的 body(剥掉 Packet 外壳才是业务载荷)。
func lastRequestBody(_ of: FakeWsTransport) throws -> Data {
    try sentPacket(of).body
}

/// 按序消费出站请求并断言其 MsgID,返回序号(供回包)。
func expectRequest(
    _ transport: FakeWsTransport, _ msgId: Chirp_Gateway_MsgID,
    file: StaticString = #filePath, line: UInt = #line
) throws -> Int64 {
    let packet = try sentPacket(transport)
    XCTAssertEqual(packet.msgID, msgId, file: file, line: line)
    return packet.sequence
}

func dmText(_ sender: String, _ receiver: String, id: String, text: String, ts: Int64)
    -> Chirp_Chat_ChatMessage
{
    var message = Chirp_Chat_ChatMessage()
    message.messageID = id
    message.senderID = sender
    message.receiverID = receiver
    message.channelType = .private
    message.channelID = SessionIndex.dmChannelId(sender, receiver)
    message.msgType = .text
    message.content = Data(text.utf8)
    message.timestamp = ts
    return message
}

/// 虚拟时钟(与 ChirpProtocolTests.ManualScheduler 同语义,本目标独立成套):
/// advance 按到期序在调用线程跑任务;窗口内新排队且仍落窗内的任务也触发。
/// 非线程安全(仅测试)。
final class ManualScheduler: Scheduler {
    private final class Task {
        let dueMs: Int64
        let seq: Int64
        let body: () -> Void
        init(dueMs: Int64, seq: Int64, body: @escaping () -> Void) {
            self.dueMs = dueMs
            self.seq = seq
            self.body = body
        }
    }

    private var queue: [Task] = []
    private var cancelled = Set<Int64>()
    private var clock: Int64
    private var ids: Int64 = 0
    private var running = false

    init(startMs: Int64 = 0) {
        clock = startMs
    }

    func nowMs() -> Int64 { clock }

    private func post0(delayMs: Int64, task: @escaping () -> Void) -> Cancellable {
        ids += 1
        let id = ids
        queue.append(Task(dueMs: clock + delayMs, seq: id, body: task))
        return FakeCancellable { [weak self] in self?.cancelled.insert(id) }
    }

    func post(delayMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        post0(delayMs: delayMs, task: task)
    }

    func postPeriodic(intervalMs: Int64, _ task: @escaping () -> Void) -> Cancellable {
        post0(delayMs: intervalMs) { [weak self] in
            self?.step(intervalMs: intervalMs, task: task)
        }
    }

    private func step(intervalMs: Int64, task: @escaping () -> Void) {
        task()
        _ = post0(delayMs: intervalMs) { [weak self] in
            self?.step(intervalMs: intervalMs, task: task)
        }
    }

    func advance(_ ms: Int64) {
        precondition(!running, "advance() is not reentrant")
        running = true
        defer { running = false }
        let target = clock + ms
        while true {
            guard let earliest = queue.min(by: { ($0.dueMs, $0.seq) < ($1.dueMs, $1.seq) })
            else { break }
            if earliest.dueMs > target { break }
            queue.removeAll { $0.seq == earliest.seq }
            if cancelled.contains(earliest.seq) { continue }
            clock = earliest.dueMs
            earliest.body()
        }
        clock = target
    }

    private final class FakeCancellable: Cancellable {
        private let body: () -> Void
        init(_ body: @escaping () -> Void) { self.body = body }
        func cancel() { body() }
    }
}

/// 恒 0 抖动(重连退避首跳 = base,确定性)。
final class FakeRandom: RandomSource {
    func nextLong(until bound: Int64) -> Int64 { 0 }
}
