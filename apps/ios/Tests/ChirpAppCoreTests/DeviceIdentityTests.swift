import XCTest
@testable import ChirpAppCore

final class DeviceIdentityTests: XCTestCase {
    func testGeneratesAndPersistsOnFirstCall() {
        var stored: String?
        let identity = DeviceIdentity(
            load: { stored },
            save: { stored = $0 },
            generate: { "fixed-uuid" }
        )
        XCTAssertEqual(identity.ensureDeviceId(), "ios-fixed-uuid")
        XCTAssertEqual(stored, "ios-fixed-uuid")
    }

    func testStableAcrossCalls() {
        // Second call must reuse the stored id, never regenerate — the
        // server kicks the previous session of the same (user, device)
        // pair, so a regenerated id would self-kick on every relaunch.
        var stored: String? = "ios-kept"
        let identity = DeviceIdentity(
            load: { stored },
            save: { stored = $0 },
            generate: { XCTFail("must not regenerate when a value is stored"); return "" }
        )
        XCTAssertEqual(identity.ensureDeviceId(), "ios-kept")
    }
}
