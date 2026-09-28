package chirp.mobile.protocol

/** Test double: captures outgoing frames, lets tests push inbound payloads. */
class FakeTransport : Transport {
    val sent = ArrayList<ByteArray>()
    var closed = false
        private set
    var onFrame: ((ByteArray) -> Unit)? = null
        private set

    override fun start(onFrame: (ByteArray) -> Unit) {
        this.onFrame = onFrame
    }

    override fun sendFrame(frame: ByteArray) {
        sent.add(frame)
    }

    override fun close() {
        closed = true
    }

    /** Deliver one complete inbound frame payload (what's inside the length prefix). */
    fun deliver(payload: ByteArray) {
        onFrame!!.invoke(payload)
    }

    /** The last outgoing frame's inner payload (length prefix stripped). */
    fun lastSentPayload(): ByteArray {
        val frame = sent.last()
        val length = ((frame[0].toInt() and 0xFF) shl 24) or
            ((frame[1].toInt() and 0xFF) shl 16) or
            ((frame[2].toInt() and 0xFF) shl 8) or
            (frame[3].toInt() and 0xFF)
        return frame.copyOfRange(4, 4 + length)
    }
}
