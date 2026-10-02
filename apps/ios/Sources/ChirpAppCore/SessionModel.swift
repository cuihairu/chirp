import Foundation

/// Login-form state and validation, phase-one semantics shared with the web
/// LoginPage and the Android shell: the user id IS the token (dev scaffold
/// mode; production credentials arrive with the auth batch).
public struct LoginDraft: Equatable {
    public var userId: String
    public var host: String

    public init(userId: String = "", host: String = "127.0.0.1:7001") {
        self.userId = userId
        self.host = host
    }

    /// Trimmed user id; the value actually sent on LOGIN.
    public var normalizedUserId: String {
        userId.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// A draft is submittable when both fields resolve (non-empty user,
    /// resolvable host). The views use this to keep the button disabled
    /// instead of surfacing errors after a tap.
    public var isValid: Bool {
        !normalizedUserId.isEmpty && HostConfig.resolve(host) != nil
    }
}

/// App-level phase for the root view switch. The connection-level states
/// (reconnecting, kicked…) ride the pipeline listener in the shell phase;
/// this enum only owns which screen is mounted.
public enum AppPhase: Equatable {
    case loggedOut
    case loggedIn(userId: String)

    public var isloggedIn: Bool {
        if case .loggedIn = self { return true }
        return false
    }
}
