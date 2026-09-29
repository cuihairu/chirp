import ChirpProtos

/// Why a request failed, independent of any server ErrorCode (port of
/// mobile_companion errors.dart via the Kotlin RequestError):
/// `.timeout` the response never arrived within the deadline; `.closed` the
/// connection dropped (pending requests are flushed on drop); `.kicked` the
/// server sent KICK_NOTIFY; `.server`/`.blocked` belong to the api/pipeline
/// layer on top and are constructible but never raised by the connection.
public struct RequestError: Error {
    public enum Kind: Equatable, CustomStringConvertible {
        case timeout
        case closed
        case kicked
        case server
        case blocked

        public var description: String {
            switch self {
            case .timeout: return "TIMEOUT"
            case .closed: return "CLOSED"
            case .kicked: return "KICKED"
            case .server: return "SERVER"
            case .blocked: return "BLOCKED"
            }
        }
    }

    public let kind: Kind
    public let code: Chirp_Common_ErrorCode?
    public let message: String?

    public init(_ kind: Kind, code: Chirp_Common_ErrorCode? = nil, message: String? = nil) {
        self.kind = kind
        self.code = code
        self.message = message
    }
}

extension RequestError: CustomStringConvertible {
    public var description: String {
        "RequestError(\(kind)): \(message ?? RequestError.defaultMessage(for: kind))"
    }

    static func defaultMessage(for kind: Kind) -> String {
        switch kind {
        case .timeout: return "请求超时"
        case .closed: return "连接已断开"
        case .kicked: return "已在其他设备登录"
        case .blocked: return "消息未发送"
        case .server: return "服务器错误"
        }
    }
}
