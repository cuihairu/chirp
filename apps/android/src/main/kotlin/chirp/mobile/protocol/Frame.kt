package chirp.mobile.protocol

// Wire framing, mirroring the C++ LengthPrefixedFramer
// (libs/network/length_prefixed_framer.cc): every frame is
//   [u32_be payload_length][payload bytes...]
// where the length prefix does NOT count itself. The gateways wrap the same
// framer inside each WebSocket binary message, so this decoder tolerates
// chunks split or coalesced at arbitrary boundaries.
// Port of mobile_companion lib/protocol/frame.dart.

/** Defensive cap; the server itself only guards the u32 range. */
const val MAX_FRAME_BYTES: Int = 16 * 1024 * 1024

/**
 * A corrupted stream cannot be resynchronized; callers treat it as a dead
 * link (the web port throws the same-shaped error).
 */
class FrameError(message: String) : Exception(message)

/** Wraps one payload into a full frame: [u32_be length][payload]. */
fun encodeFrame(payload: ByteArray): ByteArray {
    if (payload.size > MAX_FRAME_BYTES) {
        throw FrameError("frame payload too large: ${payload.size}")
    }
    val frame = ByteArray(4 + payload.size)
    frame[0] = (payload.size ushr 24).toByte()
    frame[1] = (payload.size ushr 16).toByte()
    frame[2] = (payload.size ushr 8).toByte()
    frame[3] = payload.size.toByte()
    payload.copyInto(frame, 4)
    return frame
}

/**
 * Incremental decoder for the receive direction. Feed it raw network chunks
 * in any split; it hands back every complete payload. A length prefix beyond
 * [MAX_FRAME_BYTES] raises [FrameError] and drops the buffer.
 */
class FrameDecoder {
    private var buf = ByteArray(0)

    fun feed(chunk: ByteArray): List<ByteArray> {
        val merged = ByteArray(buf.size + chunk.size)
        buf.copyInto(merged)
        chunk.copyInto(merged, buf.size)
        buf = merged

        val frames = ArrayList<ByteArray>()
        while (true) {
            if (buf.size < 4) break
            val length = ((buf[0].toInt() and 0xFF) shl 24) or
                ((buf[1].toInt() and 0xFF) shl 16) or
                ((buf[2].toInt() and 0xFF) shl 8) or
                (buf[3].toInt() and 0xFF)
            if (length > MAX_FRAME_BYTES) {
                buf = ByteArray(0)
                throw FrameError("announced frame length $length exceeds cap")
            }
            if (buf.size < 4 + length) break // half frame: wait for more
            frames.add(buf.copyOfRange(4, 4 + length))
            buf = buf.copyOfRange(4 + length, buf.size)
        }
        return frames
    }

    /** Bytes held waiting for the rest of a frame. */
    val buffered: Int get() = buf.size

    fun reset() {
        buf = ByteArray(0)
    }
}
