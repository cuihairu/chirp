package chirp.mobile.protocol

import chirp.common.Common

/**
 * Why a request failed, independent of any server ErrorCode (port of
 * mobile_companion errors.dart): [Kind.TIMEOUT] the response never arrived
 * within the deadline; [Kind.CLOSED] the connection dropped (pending
 * requests are flushed on drop); [Kind.KICKED] the server sent KICK_NOTIFY;
 * [Kind.SERVER]/[Kind.BLOCKED] belong to the api/pipeline layer on top and
 * are constructible but never raised by the connection itself.
 */
class RequestError(
    val kind: Kind,
    val code: Common.ErrorCode? = null,
    message: String? = null,
) : Exception(message ?: defaultFor(kind)) {
    enum class Kind { TIMEOUT, CLOSED, KICKED, SERVER, BLOCKED }

    override fun toString(): String = "RequestError($kind): $message"

    companion object {
        private fun defaultFor(kind: Kind): String = when (kind) {
            Kind.TIMEOUT -> "请求超时"
            Kind.CLOSED -> "连接已断开"
            Kind.KICKED -> "已在其他设备登录"
            Kind.BLOCKED -> "消息未发送"
            Kind.SERVER -> "服务器错误"
        }
    }
}
