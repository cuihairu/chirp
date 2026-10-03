import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 多端在线数据面(P4b):平台键控一槽、offline 保留、排序、reset。
final class OnlineDeviceIndexTests: XCTestCase {
    private var index: OnlineDeviceIndex!

    override func setUp() {
        super.setUp()
        index = OnlineDeviceIndex()
    }

    private func presence(
        _ platform: String, _ deviceId: String, _ online: Bool
    ) -> Chirp_Auth_DevicePresence {
        var p = Chirp_Auth_DevicePresence()
        p.platform = platform
        p.deviceID = deviceId
        p.online = online
        p.ts = 1_000
        return p
    }

    func testSamePlatformReplacesSlot() {
        // 同平台重登顶掉旧会话:一平台至多一槽,后到为准。
        index.apply(presence("web", "web-1", true), nowMs: 1_000)
        index.apply(presence("web", "web-2", true), nowMs: 1_100)
        let entries = index.entries()
        XCTAssertEqual(entries.count, 1)
        XCTAssertEqual(entries[0].deviceId, "web-2")
        XCTAssertEqual(entries[0].atMs, 1_100)
    }

    func testOfflineEntryRetainedAsLastSeen() {
        // offline 不删条目:面板展示 last-seen 形状(web 无 TTL 同口径)。
        index.apply(presence("web", "web-1", true), nowMs: 1_000)
        index.apply(presence("web", "web-1", false), nowMs: 1_200)
        let entries = index.entries()
        XCTAssertEqual(entries.count, 1)
        XCTAssertFalse(entries[0].online)
        XCTAssertEqual(entries[0].atMs, 1_200)
    }

    func testEntriesSortedByPlatform() {
        index.apply(presence("web", "w", true), nowMs: 1)
        index.apply(presence("android", "a", true), nowMs: 2)
        index.apply(presence("ios", "i", false), nowMs: 3)
        XCTAssertEqual(
            index.entries().map(\.platform), ["android", "ios", "web"])
    }

    func testBatchApplyAndEmptyPlatformNormalizes() {
        // 登录清单是批量;空 platform 归一成 default 不当独立空键。
        index.apply(
            [
                presence("web", "w", true),
                presence("", "legacy", true),
                presence("web", "w2", false),
            ], nowMs: 5_000)
        let entries = index.entries()
        XCTAssertEqual(entries.map(\.platform), ["default", "web"])
        XCTAssertEqual(entries.last?.deviceId, "w2", "批内同平台后者为准")
    }

    func testResetDropsEverything() {
        index.apply(presence("web", "w", true), nowMs: 1)
        index.reset()
        XCTAssertTrue(index.entries().isEmpty)
        // reset 后新账号事件照常从零记账。
        index.apply(presence("android", "a", true), nowMs: 2)
        XCTAssertEqual(index.entries().map(\.platform), ["android"])
    }
}
