import ChirpProtos
import Foundation

/// 多端在线数据面(P4b,web online_devices_store 同语义):本账号其他在线
/// 会话,按平台键控——同平台重登会顶掉前一会话,一平台至多一槽;
/// offline 条目保留(last-seen 形状)不删。登录响应的初始清单(服务端
/// 已排除本会话)与 DEVICES_PRESENCE_NOTIFY 增量走同一 apply。
public final class OnlineDeviceIndex {
    public struct Entry: Equatable {
        public let platform: String
        public let deviceId: String
        public let online: Bool
        /// 该平台最后一次事件的本地到达时刻(ms)。
        public let atMs: Int64

        public init(platform: String, deviceId: String, online: Bool, atMs: Int64) {
            self.platform = platform
            self.deviceId = deviceId
            self.online = online
            self.atMs = atMs
        }
    }

    private let lock = NSLock()
    private var byPlatform: [String: Entry] = [:]

    public init() {}

    /// 单条线上事件(登录清单或 notify 批次的一员)。
    public func apply(_ presence: Chirp_Auth_DevicePresence, nowMs: Int64) {
        let key = presence.platform.isEmpty ? "default" : presence.platform
        let entry = Entry(
            platform: key, deviceId: presence.deviceID,
            online: presence.online, atMs: nowMs)
        lock.lock()
        byPlatform[key] = entry
        lock.unlock()
    }

    public func apply(_ presences: [Chirp_Auth_DevicePresence], nowMs: Int64) {
        for presence in presences { apply(presence, nowMs: nowMs) }
    }

    /// 展示清单(platform 字典序,web onlineDevicesOf 同款)。
    public func entries() -> [Entry] {
        lock.lock()
        defer { lock.unlock() }
        return byPlatform.values.sorted { $0.platform < $1.platform }
    }

    /// 切号/登出:整体丢弃旧账号镜像。
    public func reset() {
        lock.lock()
        byPlatform = [:]
        lock.unlock()
    }
}
