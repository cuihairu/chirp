import XCTest
@testable import ChirpProtocol

/// Shared vector group with the dart/Kotlin frame tests.
final class FrameTests: XCTestCase {
    func testEncodeWrapsPayloadWithU32BELengthPrefix() throws {
        let frame = try encodeFrame(payload: [0x01, 0x02, 0x03])
        XCTAssertEqual(frame, [0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x03])
        XCTAssertEqual(frame.count, 7)
    }

    func testEncodeEmptyPayload() throws {
        let frame = try encodeFrame(payload: [])
        XCTAssertEqual(frame, [0x00, 0x00, 0x00, 0x00])
    }

    func testEncodeRejectsPayloadsBeyondTheCap() {
        XCTAssertThrowsError(try encodeFrame(payload: [UInt8](repeating: 0, count: MAX_FRAME_BYTES + 1))) {
            XCTAssertTrue($0 is FrameError)
        }
    }

    func testRoundTripAndSplitFeeds() throws {
        let payload = Array("hello chirp frame".utf8)
        let frame = try encodeFrame(payload: payload)
        let decoder = FrameDecoder()
        // Feed byte-by-byte: every split boundary must be tolerated.
        var out: [[UInt8]] = []
        for b in frame {
            out.append(contentsOf: try decoder.feed([b]))
        }
        XCTAssertEqual(out, [payload])
        XCTAssertEqual(decoder.buffered, 0)
    }

    func testCoalescedFramesInOneChunk() throws {
        let one = try encodeFrame(payload: [0x0A])
        let two = try encodeFrame(payload: [0x0B, 0x0C])
        let decoder = FrameDecoder()
        let out = try decoder.feed(one + two)
        XCTAssertEqual(out, [[0x0A], [0x0B, 0x0C]])
    }

    func testOverlongPrefixThrowsAndDropsTheBuffer() throws {
        let decoder = FrameDecoder()
        // A valid frame first, then a lying prefix: the error must also
        // discard the bytes held for the (already delivered) frame.
        _ = try decoder.feed(try encodeFrame(payload: [0x01]))
        let evil: [UInt8] = [0x7F, 0x00, 0x00, 0x00]
        XCTAssertThrowsError(try decoder.feed(evil)) { error in
            XCTAssertTrue(error is FrameError)
        }
        XCTAssertEqual(decoder.buffered, 0)
        // The decoder keeps working for a fresh well-formed frame.
        let recovered = try decoder.feed(try encodeFrame(payload: [0x09]))
        XCTAssertEqual(recovered, [[0x09]])
    }
}
