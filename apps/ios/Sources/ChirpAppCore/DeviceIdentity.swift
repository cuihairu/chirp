import Foundation

/// Stable per-install device id (`ios-<uuid>`), port of the web
/// `ensureDeviceId` / Android `deviceId()` pair: the server kicks the
/// previous session of the same (user, device) pair, so a stable id means an
/// ordinary relaunch reconnects while a second install takes over.
///
/// Storage and id generation are injected so tests run on Linux without
/// UserDefaults: the app shell passes a UserDefaults-backed closure pair.
public struct DeviceIdentity {
    /// Reads the persisted id (nil = never generated).
    public let load: () -> String?
    /// Persists the id for the next launch.
    public let save: (String) -> Void
    /// Produces a fresh id when none is stored; UUID by default.
    public let generate: () -> String

    public init(
        load: @escaping () -> String?,
        save: @escaping (String) -> Void,
        generate: @escaping () -> String = { UUID().uuidString }
    ) {
        self.load = load
        self.save = save
        self.generate = generate
    }

    /// Returns the stored id, generating and persisting one on first call.
    /// Idempotent: after the first call every later call returns the same id.
    public func ensureDeviceId() -> String {
        if let existing = load() {
            return existing
        }
        let fresh = "ios-\(generate())"
        save(fresh)
        return fresh
    }
}
