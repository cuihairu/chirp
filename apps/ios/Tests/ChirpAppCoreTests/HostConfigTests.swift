import XCTest
@testable import ChirpAppCore

final class HostConfigTests: XCTestCase {
    func testBareHostDefaultsToWsAndChatPort() {
        // Bare host gets ws:// and keeps the chat port as written.
        let config = HostConfig.resolve("10.1.2.3:7001")
        XCTAssertEqual(config?.chatUrl.absoluteString, "ws://10.1.2.3:7001")
        XCTAssertEqual(config?.deviceUrl.absoluteString, "ws://10.1.2.3:5201")
        XCTAssertEqual(config?.partyUrl.absoluteString, "ws://10.1.2.3:7501")
        XCTAssertEqual(config?.voiceUrl.absoluteString, "ws://10.1.2.3:9001")
    }

    func testFullChatUrlDropsPath() {
        // The /ws/chat form is vite-proxy routing; the gateway serves at the
        // root, so the path is dropped, scheme/host/port kept as-is.
        let config = HostConfig.resolve("wss://chat.example.com:8443/ws/chat")
        XCTAssertEqual(config?.chatUrl.absoluteString, "wss://chat.example.com:8443")
        XCTAssertEqual(config?.deviceUrl.absoluteString, "ws://chat.example.com:5201")
        XCTAssertEqual(config?.partyUrl.absoluteString, "ws://chat.example.com:7501")
        XCTAssertEqual(config?.voiceUrl.absoluteString, "ws://chat.example.com:9001")
    }

    func testSimulatorDefaultPointsAtLoopback() {
        // iOS simulator reaches the Mac's loopback directly (no 10.0.2.2
        // alias like Android emulators).
        XCTAssertEqual(HostConfig.simulatorDefault.chatUrl.absoluteString, "ws://127.0.0.1:7001")
        XCTAssertEqual(HostConfig.simulatorDefault.deviceUrl.absoluteString, "ws://127.0.0.1:5201")
        XCTAssertEqual(HostConfig.simulatorDefault.partyUrl.absoluteString, "ws://127.0.0.1:7501")
        XCTAssertEqual(HostConfig.simulatorDefault.voiceUrl.absoluteString, "ws://127.0.0.1:9001")
    }

    func testRejectsEmptyAndGarbage() {
        XCTAssertNil(HostConfig.resolve("   "))
        XCTAssertNil(HostConfig.resolve("http://"))
    }
}
