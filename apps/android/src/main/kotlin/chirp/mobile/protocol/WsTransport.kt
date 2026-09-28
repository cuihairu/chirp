package chirp.mobile.protocol

/**
 * Transport seam between the connection state machine and the platform
 * socket (port of mobile_companion ws_transport.dart WsTransport). The
 * connection owns the u32 framing; this layer moves raw WebSocket binary
 * messages. Tests inject a scripted fake; the Android batch uses
 * [OkHttpTransport].
 */
interface WsTransport {
    /**
     * Open the socket. [onResult] fires exactly once: `null` when open, or
     * the failure when the attempt failed — after an open failure [onClosed]
     * never fires (the connection runs its close path itself, mirroring the
     * dart client).
     */
    fun open(onResult: (Throwable?) -> Unit)

    /** Register the binary-message handler (call before [open]). Text frames never occur on this protocol; implementations drop them. */
    fun onBinary(handler: (ByteArray) -> Unit)

    /**
     * Register the down handler — fires exactly once when the socket is down:
     * after [close], after a remote close, or after an error. Never fires
     * before [open] completes.
     */
    fun onClosed(handler: () -> Unit)

    /** Send one binary message. False when the socket is already down. */
    fun send(data: ByteArray): Boolean

    /** Best-effort graceful close; [onClosed] still fires. */
    fun close()
}
