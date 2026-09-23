using System;

namespace Chirp.Sdk
{
    /// <summary>Why a request failed, independent of any server ErrorCode:
    /// Timeout — the response never arrived within the deadline; Closed — the
    /// connection dropped (pending requests are flushed on drop); Kicked —
    /// the server sent KICK_NOTIFY and closed the socket; Server — a response
    /// arrived and carried a non-OK ErrorCode; Blocked — the message never
    /// went on the wire (local validation, interceptor drop or '/' command
    /// routing).</summary>
    public enum RequestErrorKind
    {
        Timeout,
        Closed,
        Kicked,
        Server,
        Blocked,
    }

    public sealed class RequestError : Exception
    {
        public RequestError(RequestErrorKind kind, Chirp.Common.ErrorCode? code = null,
            string? message = null)
            : base(message ?? DefaultMessage(kind, code))
        {
            Kind = kind;
            Code = code;
        }

        public RequestErrorKind Kind { get; }

        /// <summary>Server ErrorCode when Kind == Server.</summary>
        public Chirp.Common.ErrorCode? Code { get; }

        private static string DefaultMessage(RequestErrorKind kind, Chirp.Common.ErrorCode? code)
        {
            switch (kind)
            {
                case RequestErrorKind.Timeout:
                    return "请求超时";
                case RequestErrorKind.Closed:
                    return "连接已断开";
                case RequestErrorKind.Kicked:
                    return "已在其他设备登录";
                case RequestErrorKind.Blocked:
                    return "消息未发送";
                default:
                    return ChirpErrorText.Of(code ?? Chirp.Common.ErrorCode.InternalError);
            }
        }
    }

    /// <summary>Chinese copy for every server ErrorCode.</summary>
    public static class ChirpErrorText
    {
        public static string Of(Chirp.Common.ErrorCode code)
        {
            switch (code)
            {
                case Chirp.Common.ErrorCode.Ok:
                    return "成功";
                case Chirp.Common.ErrorCode.InternalError:
                    return "服务器内部错误";
                case Chirp.Common.ErrorCode.InvalidParam:
                    return "参数错误";
                case Chirp.Common.ErrorCode.AuthFailed:
                    return "认证失败";
                case Chirp.Common.ErrorCode.SessionExpired:
                    return "登录已过期,请重新登录";
                case Chirp.Common.ErrorCode.UserNotFound:
                    return "用户不存在";
                // TargetOffline is NOT a send failure: the server queued the
                // message into the recipient's offline queue and will push it
                // on reconnect.
                case Chirp.Common.ErrorCode.TargetOffline:
                    return "对方离线,消息将在其上线后送达";
                case Chirp.Common.ErrorCode.ServerUnavailable:
                    return "服务暂不可用,请稍后再试";
                case Chirp.Common.ErrorCode.RateLimited:
                    return "操作过于频繁,请稍后再试";
                default:
                    return $"未知错误({(int)code})";
            }
        }
    }
}
