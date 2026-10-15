import XCTest
import ChirpProtos
@testable import ChirpProtocol

/// 消息压缩消费面：gzip 原语 + GET_HISTORY 双形态拆封
/// (game_chat_features P2)。与 C++/TS/C#/Kotlin 四端同形向量对拍。
///
/// 容器在测试里手搓（stored deflate 块 + 自算 CRC32/ISIZE）：Swift 与 Linux
/// Foundation 都没有 gzip 压缩面，而 inflate 面是生产代码真正依赖的那一半，
/// 所以这里只补压缩侧的最小可用工具。
final class GzipTests: XCTestCase {
    // ---- gzip container builder (RFC 1952 + stored deflate blocks) ----

    private static let crcTable: [UInt32] = (0..<256).map { index -> UInt32 in
        var value = UInt32(index)
        for _ in 0..<8 {
            value = (value & 1) != 0 ? (0xEDB88320 ^ (value >> 1)) : (value >> 1)
        }
        return value
    }

    private static func crc32(_ data: [UInt8]) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data {
            crc = crcTable[Int((crc ^ UInt32(byte)) & 0xFF)] ^ (crc >> 8)
        }
        return crc ^ 0xFFFF_FFFF
    }

    private func gzip(_ payload: [UInt8]) -> [UInt8] {
        var out: [UInt8] = [0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03]
        var offset = 0
        repeat {
            let len = min(65535, payload.count - offset)
            // BFINAL in bit 0, BTYPE = 00 (stored); stored blocks are byte
            // aligned, so the 3 header bits sit at the start of a fresh byte.
            out.append(offset + len >= payload.count ? 1 : 0)
            let lenLE = UInt16(len)
            let nlenLE = ~lenLE
            out.append(UInt8(lenLE & 0xFF))
            out.append(UInt8(lenLE >> 8))
            out.append(UInt8(nlenLE & 0xFF))
            out.append(UInt8(nlenLE >> 8))
            out.append(contentsOf: payload[offset ..< offset + len])
            offset += len
        } while offset < payload.count

        let crc = Self.crc32(payload)
        let size = UInt32(truncatingIfNeeded: payload.count)
        for value in [crc, size] {
            out.append(UInt8(value & 0xFF))
            out.append(UInt8((value >> 8) & 0xFF))
            out.append(UInt8((value >> 16) & 0xFF))
            out.append(UInt8((value >> 24) & 0xFF))
        }
        return out
    }

    // ---- primitive ----

    func testRoundTripKeepsEveryByte() throws {
        let payload = (0 ..< 4096).map { UInt8(($0 &* 31 &+ $0 / 7) & 0xFF) }

        XCTAssertEqual(try Gzip.decompress(gzip(payload)), payload)
    }

    func testRoundTripGrowsPastTheFirstBuffer() throws {
        // 300 KiB spans several stored blocks and forces the growing buffer to
        // re-run inflate past its 64 KiB first guess.
        var payload = [UInt8](repeating: 0, count: 300_000)
        var state: UInt32 = 0x1234_5678
        for index in payload.indices {
            state = state &* 1_103_515_245 &+ 12_345
            payload[index] = UInt8((state >> 24) & 0xFF)
        }

        XCTAssertEqual(try Gzip.decompress(gzip(payload)), payload)
    }

    func testEmptyInputRaises() {
        XCTAssertThrowsError(try Gzip.decompress([])) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.emptyInput))
        }
    }

    func testCorruptContainerRaises() {
        var container = gzip([UInt8](repeating: 0xA5, count: 300_000))
        container[container.count / 2] ^= 0xFF

        XCTAssertThrowsError(try Gzip.decompress(container)) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.corruptOrTruncated))
        }
    }

    func testTruncatedContainerRaises() {
        let container = gzip([UInt8](repeating: 0x5A, count: 300_000))

        // Cut inside the deflate body: the trailer never arrives, so the
        // stream cannot end — this is the case C#'s GZipStream swallows
        // without the explicit trailer check.
        XCTAssertThrowsError(try Gzip.decompress(Array(container.prefix(container.count / 2)))) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.corruptOrTruncated))
        }
    }

    func testCorruptTrailerRaises() {
        var container = gzip((0 ..< 1024).map { UInt8($0 & 0xFF) })
        container[container.count - 5] ^= 0xFF

        XCTAssertThrowsError(try Gzip.decompress(container)) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.corruptOrTruncated))
        }
    }

    func testHeaderOnlyContainerRaises() {
        // RFC 1952 header with no deflate body and no trailer.
        let container: [UInt8] = [0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0, 0x03]

        XCTAssertThrowsError(try Gzip.decompress(container)) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.corruptOrTruncated))
        }
    }

    func testOverCapRaises() {
        // 17 MiB of zeros: inflation must abort at the 16 MiB cap instead of
        // buffering the whole body.
        let container = gzip([UInt8](repeating: 0, count: 17 * 1024 * 1024))

        XCTAssertThrowsError(try Gzip.decompress(container)) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.overCap))
        }
    }

    // ---- spec surface ----

    func testGetHistoryInflatesTheGzipForm() throws {
        let inner = Chirp_Chat_GetHistoryResponse.with {
            $0.hasMore_p = true
            $0.messages = [
                Chirp_Chat_ChatMessage.with { $0.messageID = "m1"; $0.senderID = "u1" },
                Chirp_Chat_ChatMessage.with { $0.messageID = "m2"; $0.senderID = "u2" },
            ]
        }
        let outer = Chirp_Chat_GetHistoryResponse.with {
            $0.hasMore_p = true
            $0.messagesGzip = Data(gzip(inner.serializedData()))
        }

        let resp = try MsgSpecs.getHistory.decodeResponse([UInt8](outer.serializedData()))

        XCTAssertEqual(resp.code, .ok)
        XCTAssertTrue(resp.hasMore_p)
        XCTAssertTrue(resp.messagesGzip.isEmpty)
        XCTAssertEqual(resp.messages.map(\.messageID), ["m1", "m2"])
        XCTAssertEqual(resp.messages.map(\.senderID), ["u1", "u2"])
    }

    func testGetHistoryAcceptsThePlainForm() throws {
        let body = Chirp_Chat_GetHistoryResponse.with {
            $0.messages = [Chirp_Chat_ChatMessage.with { $0.messageID = "m9"; $0.senderID = "u9" }]
        }

        let resp = try MsgSpecs.getHistory.decodeResponse([UInt8](body.serializedData()))

        XCTAssertEqual(resp.messages.map(\.messageID), ["m9"])
    }

    func testGetHistoryRejectsACorruptContainer() {
        var container = gzip([UInt8](repeating: 0xA5, count: 300_000))
        container[container.count / 2] ^= 0xFF
        let outer = Chirp_Chat_GetHistoryResponse.with { $0.messagesGzip = Data(container) }

        XCTAssertThrowsError(try MsgSpecs.getHistory.decodeResponse([UInt8](outer.serializedData()))) { error in
            XCTAssertEqual(error as? Gzip.Error, Gzip.Error(.corruptOrTruncated))
        }
    }

    func testGetHistoryRejectsAGarbagePayload() {
        // A valid container whose payload is not a GetHistoryResponse: proto
        // parsing must fail, the same BadResponse-equivalent outcome the other
        // four runtimes report for the identical case.
        let outer = Chirp_Chat_GetHistoryResponse.with {
            $0.messagesGzip = Data(gzip([0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x11, 0x22, 0x33]))
        }

        XCTAssertThrowsError(try MsgSpecs.getHistory.decodeResponse([UInt8](outer.serializedData())))
    }
}