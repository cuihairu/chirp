import 'package:chirp_proto/chirp_proto.dart';

/// Why a request failed, independent of any server ErrorCode:
///  - timeout: the response never arrived within the deadline
///  - closed:  the connection dropped (pending requests are flushed on drop)
///  - kicked:  the server sent KICK_NOTIFY and closed the socket
///  - server:  a response arrived and carried a non-OK ErrorCode
///  - blocked: the message never went on the wire (interceptor drop, or a
///             '/'-command consumed locally while handlers are registered)
enum RequestErrorKind { timeout, closed, kicked, server, blocked }

class RequestError implements Exception {
  RequestError(this.kind, {this.code, String? message})
      : message = message ?? defaultMessage(kind, code);

  final RequestErrorKind kind;

  /// Server ErrorCode when kind == RequestErrorKind.server.
  final ErrorCode? code;

  final String message;

  @override
  String toString() => 'RequestError($kind, ${code?.name ?? ''}): $message';

  static String defaultMessage(RequestErrorKind kind, ErrorCode? code) {
    switch (kind) {
      case RequestErrorKind.timeout:
        return '请求超时';
      case RequestErrorKind.closed:
        return '连接已断开';
      case RequestErrorKind.kicked:
        return '已在其他设备登录';
      case RequestErrorKind.blocked:
        return '消息未发送';
      case RequestErrorKind.server:
        return errorText(code ?? ErrorCode.INTERNAL_ERROR);
    }
  }
}

/// Chinese copy for every server ErrorCode.
String errorText(ErrorCode code) {
  switch (code) {
    case ErrorCode.OK:
      return '成功';
    case ErrorCode.INTERNAL_ERROR:
      return '服务器内部错误';
    case ErrorCode.INVALID_PARAM:
      return '参数错误';
    case ErrorCode.AUTH_FAILED:
      return '认证失败';
    case ErrorCode.SESSION_EXPIRED:
      return '登录已过期,请重新登录';
    case ErrorCode.USER_NOT_FOUND:
      return '用户不存在';
    // TARGET_OFFLINE is NOT a send failure: the server queued the message
    // into the recipient's offline queue and will push it on reconnect.
    case ErrorCode.TARGET_OFFLINE:
      return '对方离线,消息将在其上线后送达';
    case ErrorCode.SERVER_UNAVAILABLE:
      return '服务暂不可用,请稍后再试';
    case ErrorCode.RATE_LIMITED:
      return '操作过于频繁,请稍后再试';
    default:
      return '未知错误(${code.value})';
  }
}
