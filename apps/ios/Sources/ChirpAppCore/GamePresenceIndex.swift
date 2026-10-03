import Foundation

/// 游戏在线状态镜像(P4d,web game_presence_api.ts 同语义):走设备面
/// (5201),只有开关 + 游戏清单——绑定关系是游戏侧的事,伴侣只读。
/// 面失败即 unavailable,UI 降级隐藏(web 同款)。
public final class GamePresenceIndex {
    public struct State: Equatable {
        public var enabled: Bool
        /// 有 presence 的 gameId 列表(enabled && bound;禁用时恒空)。
        public var gameIds: [String]

        public init(enabled: Bool = false, gameIds: [String] = []) {
            self.enabled = enabled
            self.gameIds = gameIds
        }
    }

    private let lock = NSLock()
    private var stateBox = State()
    private var unavailableBox = false

    public init() {}

    /// GET_GAME_PRESENCE 应答换入。
    public func apply(_ state: State) {
        lock.lock()
        stateBox = state
        unavailableBox = false
        lock.unlock()
    }

    /// 开关本地回显(SET_GAME_PRESENCE_ENABLED 成功后、复核拉取前)。
    public func setEnabled(_ enabled: Bool) {
        lock.lock()
        stateBox.enabled = enabled
        if !enabled { stateBox.gameIds = [] }
        unavailableBox = false
        lock.unlock()
    }

    /// 面失败:数据不可信,UI 降级。
    public func markUnavailable() {
        lock.lock()
        unavailableBox = true
        lock.unlock()
    }

    // ---- 读面 ---------------------------------------------------------------

    public func state() -> State {
        lock.lock()
        defer { lock.unlock() }
        return stateBox
    }

    public func unavailable() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return unavailableBox
    }
}
