import Foundation

// Wire framing, mirroring the C++ LengthPrefixedFramer
// (libs/network/length_prefixed_framer.cc): every frame is
//   [u32_be payload_length][payload bytes...]
// where the length prefix does NOT count itself. The gateways wrap the same
// framer inside each WebSocket binary message, so this decoder tolerates
// chunks split or coalesced at arbitrary boundaries.
// Port of mobile_companion lib/protocol/frame.dart (via the Kotlin Frame.kt
// whose test vectors this package shares).

/// Defensive cap; the server itself only guards the u32 range.
public let MAX_FRAME_BYTES: Int = 16 * 1024 * 1024

/// A corrupted stream cannot be resynchronized; callers treat it as a dead
/// link (the web port throws the same-shaped error).
public struct FrameError: Error, CustomStringConvertible {
    public let message: String
    public init(_ message: String) { self.message = message }
    public var description: String { "FrameError: \(message)" }
}

/// Wraps one payload into a full frame: [u32_be length][payload].
public func encodeFrame(payload: [UInt8]) throws -> [UInt8] {
    if payload.count > MAX_FRAME_BYTES {
        throw FrameError("frame payload too large: \(payload.count)")
    }
    var frame = [UInt8](repeating: 0, count: 4 + payload.count)
    let n = UInt32(payload.count)
    frame[0] = UInt8((n >> 24) & 0xFF)
    frame[1] = UInt8((n >> 16) & 0xFF)
    frame[2] = UInt8((n >> 8) & 0xFF)
    frame[3] = UInt8(n & 0xFF)
    frame.replaceSubrange(4..., with: payload)
    return frame
}

/// Incremental decoder for the receive direction. Feed it raw network chunks
/// in any split; it hands back every complete payload. A length prefix beyond
/// `MAX_FRAME_BYTES` throws `FrameError` and drops the buffer.
public final class FrameDecoder {
    private var buf: [UInt8] = []

    public init() {}

    public func feed(_ chunk: [UInt8]) throws -> [[UInt8]] {
        buf.append(contentsOf: chunk)

        var frames: [[UInt8]] = []
        while true {
            if buf.count < 4 { break }
            let length = Int(buf[0]) << 24 | Int(buf[1]) << 16 | Int(buf[2]) << 8 | Int(buf[3])
            if length > MAX_FRAME_BYTES {
                buf = []
                throw FrameError("announced frame length \(length) exceeds cap")
            }
            if buf.count < 4 + length { break } // half frame: wait for more
            frames.append(Array(buf[4..<(4 + length)]))
            buf.removeFirst(4 + length)
        }
        return frames
    }

    /// Bytes held waiting for the rest of a frame.
    public var buffered: Int { buf.count }

    public func reset() {
        buf = []
    }
}
