import XCTest
@testable import ChirpAppCore

final class LoginDraftTests: XCTestCase {
    func testValidWhenUserAndHostResolve() {
        let draft = LoginDraft(userId: "  alice  ", host: "127.0.0.1:7001")
        XCTAssertTrue(draft.isValid)
        XCTAssertEqual(draft.normalizedUserId, "alice")
    }

    func testInvalidOnBlankUserOrUnresolvableHost() {
        XCTAssertFalse(LoginDraft(userId: "   ", host: "127.0.0.1:7001").isValid)
        XCTAssertFalse(LoginDraft(userId: "alice", host: "   ").isValid)
    }

    func testInitialPhaseLoggedOut() {
        XCTAssertEqual(AppPhase.loggedOut, .loggedOut)
        XCTAssertFalse(AppPhase.loggedOut.isloggedIn)
        XCTAssertTrue(AppPhase.loggedIn(userId: "alice").isloggedIn)
    }
}
