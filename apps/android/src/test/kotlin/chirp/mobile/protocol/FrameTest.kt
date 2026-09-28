package chirp.mobile.protocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class FrameTest {
    @Test
    fun encodePrependsBigEndianLengthThatDoesNotCountItself() {
        val frame = encodeFrame(byteArrayOf(1, 2, 3))
        assertEquals(7, frame.size)
        assertEquals(0, frame[0].toInt())
        assertEquals(0, frame[1].toInt())
        assertEquals(0, frame[2].toInt())
        assertEquals(3, frame[3].toInt())
        assertContentEquals(byteArrayOf(1, 2, 3), frame.copyOfRange(4, 7))
    }

    @Test
    fun encodeRejectsPayloadsBeyondTheCap() {
        val err = assertFailsWith<FrameError> {
            encodeFrame(ByteArray(MAX_FRAME_BYTES + 1))
        }
        assertTrue(err.message!!.contains("too large"))
    }

    @Test
    fun decoderReassemblesChunksSplitAtEveryBoundary() {
        val payload = ByteArray(1021) { (it % 251).toByte() }
        val frame = encodeFrame(payload)
        val decoder = FrameDecoder()
        val out = ArrayList<ByteArray>()
        for (b in frame) out += decoder.feed(byteArrayOf(b))
        assertEquals(1, out.size)
        assertContentEquals(payload, out[0])
        assertEquals(0, decoder.buffered)
    }

    @Test
    fun decoderSplitsCoalescedFramesInOneChunk() {
        val decoder = FrameDecoder()
        val merged = encodeFrame(byteArrayOf(9, 9)) + encodeFrame(byteArrayOf(7)) +
            encodeFrame(ByteArray(0))
        val out = decoder.feed(merged)
        assertEquals(3, out.size)
        assertContentEquals(byteArrayOf(9, 9), out[0])
        assertContentEquals(byteArrayOf(7), out[1])
        assertEquals(0, out[2].size)
    }

    @Test
    fun decoderKeepsHalfFramesWaitingForTheRest() {
        val decoder = FrameDecoder()
        val frame = encodeFrame(byteArrayOf(1, 2, 3, 4, 5))
        val out = ArrayList<ByteArray>()
        out += decoder.feed(frame.copyOfRange(0, 5))
        assertEquals(0, out.size)
        assertEquals(5, decoder.buffered)
        out += decoder.feed(frame.copyOfRange(5, frame.size))
        assertEquals(1, out.size)
        assertContentEquals(byteArrayOf(1, 2, 3, 4, 5), out[0])
    }

    @Test
    fun oversizePrefixRaisesFrameErrorAndDropsTheBuffer() {
        val decoder = FrameDecoder()
        val evil = ByteArray(4)
        evil[0] = 0x7F // 0x7F000000 ≈ 2.1 GB, well beyond the 16 MiB cap
        val err = assertFailsWith<FrameError> { decoder.feed(evil) }
        assertTrue(err.message!!.contains("exceeds cap"))
        assertEquals(0, decoder.buffered)
    }
}
