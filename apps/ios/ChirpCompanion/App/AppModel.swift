import ChirpAppCore
import SwiftUI

/// Root observable state. Thin by design: everything testable lives in
/// ChirpAppCore (package targets run under `swift test` on Linux too); this
/// type only bridges those values into SwiftUI.
@MainActor
final class AppModel: ObservableObject {
    /// Login form state (user id + editable host).
    @Published var draft = LoginDraft()
    /// Which screen the root view mounts.
    @Published private(set) var phase: AppPhase = .loggedOut

    /// Set once the draft passed validation. P1 (工程骨架) stops here on
    /// purpose — the login round-trip, pipeline wiring and session state
    /// land in the next batch, so the app never pretends to be logged in.
    @Published private(set) var validatedDraft: LoginDraft?

    /// Stable per-install device id (server kick semantics: a stable id
    /// reconnects, a fresh one takes the session over).
    let deviceId: String

    init() {
        let defaults = UserDefaults.standard
        let identity = DeviceIdentity(
            load: { defaults.string(forKey: Self.deviceIdKey) },
            save: { defaults.set($0, forKey: Self.deviceIdKey) }
        )
        deviceId = identity.ensureDeviceId()
    }

    /// Validates the form and records the draft. The actual connect/login
    /// (ChatPipeline over WsTransportDarwin) is the next increment.
    func loginTapped() {
        guard draft.isValid else { return }
        validatedDraft = draft
    }

    private static let deviceIdKey = "chirp.device_id"
}
