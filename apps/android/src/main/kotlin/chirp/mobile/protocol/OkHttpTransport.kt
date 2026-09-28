package chirp.mobile.protocol

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString

/**
 * OkHttp WebSocket adapter (the dart port's IoWebSocket role). Works on every
 * Android API level — java.net.http's WebSocket only exists from API 34, so
 * OkHttp stays the production transport for the Android shell.
 *
 * Callback contract matches [WsTransport]: [open]'s result fires exactly
 * once; [onClosed] fires exactly once and only after a successful open (a
 * pre-open failure reports through [open] instead — the connection runs its
 * own close path, same as the dart client's openIt catch).
 */
class OkHttpTransport(
    private val url: String,
    private val client: OkHttpClient,
) : WsTransport {
    private var binaryHandler: ((ByteArray) -> Unit)? = null
    private var closedHandler: (() -> Unit)? = null
    private var ws: WebSocket? = null
    private var openAnnounced = false
    private var closedAnnounced = false

    override fun open(onResult: (Throwable?) -> Unit) {
        ws = client.newWebSocket(
            Request.Builder().url(url).build(),
            object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    if (!openAnnounced) {
                        openAnnounced = true
                        onResult(null)
                    }
                }

                override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                    binaryHandler?.invoke(bytes.toByteArray())
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    // Text frames are not part of this protocol; drop them.
                }

                override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                    // Peer-initiated close: the socket is going down and
                    // okhttp answers the close frame automatically. Announce
                    // here — the full handshake to onClosed never completes
                    // under MockWebServer, and real abort paths skip it too.
                    announceClosed()
                }

                override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                    announceClosed()
                }

                override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                    if (!openAnnounced) {
                        openAnnounced = true
                        onResult(t)
                    } else {
                        announceClosed()
                    }
                }
            },
        )
    }

    private fun announceClosed() {
        if (closedAnnounced) return
        closedAnnounced = true
        closedHandler?.invoke()
    }

    override fun onBinary(handler: (ByteArray) -> Unit) {
        binaryHandler = handler
    }

    override fun onClosed(handler: () -> Unit) {
        closedHandler = handler
    }

    override fun send(data: ByteArray): Boolean {
        val w = ws ?: return false
        return w.send(ByteString.of(*data))
    }

    override fun close() {
        // 1000 normal closure; the server's close frame (or onFailure) drives
        // the closed announcement.
        ws?.close(1000, null)
    }
}
