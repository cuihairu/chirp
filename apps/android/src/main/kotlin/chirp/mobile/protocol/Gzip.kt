package chirp.mobile.protocol

import chirp.chat.Chat
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.GZIPInputStream

/**
 * gzip reader for the history-readback compression shape (game_chat_features
 * P2 消息压缩). A session that declared supportsGzip at login gets the history
 * message list inside GetHistoryResponse.messagesGzip: a gzip (RFC 1952)
 * container holding a same-shape GetHistoryResponse. GZIPInputStream is the
 * JVM decoder — it reads RFC 1952 unconfigured (and validates the CRC/ISIZE
 * trailer itself, so a container cut short mid-body raises instead of handing
 * up a short message list), which is why every runtime consumes the container
 * unchanged.
 */

/** Decompression-bomb cap; the C++ and TS primitives use the same bound. */
const val MAX_DECOMPRESSED_BYTES: Int = 16 * 1024 * 1024

/** Raised for empty input, a corrupt/truncated container, or the size cap. */
class GzipError(message: String) : RuntimeException(message)

/**
 * Inflate a gzip container. Streaming the transform keeps the memory bound:
 * the reader aborts as soon as the inflated size crosses the cap instead of
 * buffering an unbounded body.
 */
fun gzipDecompress(bytes: ByteArray): ByteArray {
    if (bytes.isEmpty()) {
        throw GzipError("gzip: empty input")
    }

    val output = ByteArrayOutputStream()
    try {
        GZIPInputStream(ByteArrayInputStream(bytes)).use { source ->
            val buffer = ByteArray(8192)
            while (true) {
                val read = source.read(buffer)
                if (read < 0) {
                    break
                }
                output.write(buffer, 0, read)
                if (output.size() > MAX_DECOMPRESSED_BYTES) {
                    throw GzipError("gzip: decompressed size exceeds the cap")
                }
            }
        }
    } catch (error: GzipError) {
        throw error
    } catch (error: IOException) {
        throw GzipError("gzip: corrupt or truncated container")
    }
    return output.toByteArray()
}

/**
 * Open the compressed-history envelope into one uniform shape: the inflated
 * list replaces messagesGzip, the outer code/has_more stay as sent, and the
 * plain form passes through untouched. A corrupt container or an unparseable
 * payload raises — the caller sees the readback fail rather than quietly
 * losing history.
 */
internal fun inflateGzipHistory(resp: Chat.GetHistoryResponse): Chat.GetHistoryResponse {
    if (resp.messagesGzip.isEmpty) {
        return resp
    }
    val inner = Chat.GetHistoryResponse.parseFrom(gzipDecompress(resp.messagesGzip.toByteArray()))
    return resp.toBuilder()
        .clearMessagesGzip()
        .clearMessages()
        .addAllMessages(inner.messagesList)
        .build()
}