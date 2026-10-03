import ChirpProtos
import Foundation

/// 语音房间镜像(P4d,web voice_store.ts 同语义,协议层无媒体):服务端
/// 权威但 roster 变更 notify 排除操作者(join/leave/mute 都带
/// exclude_user),镜像从三个方向拼:整表拉取(join/create/restore 后的
/// GET_ROOM_INFO)、无操作者的 notify、自己 mute/deafen 的本地回显。
public final class VoiceIndex {
    public struct Participant: Equatable {
        public let userId: String
        /// 服务端推导态(DEAFENED > MUTED > CONNECTED);notify 只改这个。
        public var state: Chirp_Voice_ParticipantState
        /// 标志细节只有整表拉取才有;notify 只带 state。
        public var muted: Bool?
        public var deafened: Bool?

        public init(
            userId: String, state: Chirp_Voice_ParticipantState,
            muted: Bool? = nil, deafened: Bool? = nil
        ) {
            self.userId = userId
            self.state = state
            self.muted = muted
            self.deafened = deafened
        }
    }

    /// 服务端 GetRoomInfoResponse 的窄拷贝(UI 渲染面)。
    public struct RoomSnapshot: Equatable {
        public let roomId: String
        public let roomName: String
        public let maxParticipants: Int32
        public var participants: [Participant]

        public init(
            roomId: String, roomName: String, maxParticipants: Int32,
            participants: [Participant]
        ) {
            self.roomId = roomId
            self.roomName = roomName
            self.maxParticipants = maxParticipants
            self.participants = participants
        }
    }

    private let lock = NSLock()
    private var roomBox: RoomSnapshot?

    public init() {}

    /// 整 roster 换入(GET_ROOM_INFO 应答)。
    public func apply(_ snapshot: RoomSnapshot) {
        lock.lock()
        roomBox = snapshot
        lock.unlock()
    }

    /// 离开(或 restore 失败)整体丢弃。
    public func clear() {
        lock.lock()
        roomBox = nil
        lock.unlock()
    }

    /// PARTICIPANT_JOINED_NOTIFY(永不是自己——操作者被排除)。调用方
    /// 已守卫 roomId 匹配;这里按 userId 去旧插新。
    public func upsert(_ participant: Participant) {
        lock.lock()
        defer { lock.unlock() }
        guard roomBox != nil else { return }
        roomBox!.participants.removeAll { $0.userId == participant.userId }
        roomBox!.participants.append(participant)
    }

    /// PARTICIPANT_LEFT_NOTIFY;陈旧 roomId(我们已退房)忽略。
    public func removeParticipant(roomId: String, userId: String) {
        lock.lock()
        defer { lock.unlock() }
        guard roomBox?.roomId == roomId else { return }
        roomBox!.participants.removeAll { $0.userId == userId }
    }

    /// PARTICIPANT_STATE_CHANGED_NOTIFY;只改 state,标志细节保留到
    /// 下次整表拉取。陈旧 roomId 忽略。
    public func updateState(
        roomId: String, userId: String, state: Chirp_Voice_ParticipantState
    ) {
        lock.lock()
        defer { lock.unlock() }
        guard roomBox?.roomId == roomId else { return }
        if let index = roomBox!.participants.firstIndex(where: { $0.userId == userId }) {
            roomBox!.participants[index].state = state
        }
    }

    /// 自己 mute/deafen 的本地回显(state 广播排除了操作者)。
    public func applySelfFlags(
        userId: String, muted: Bool? = nil, deafened: Bool? = nil
    ) {
        lock.lock()
        defer { lock.unlock() }
        guard let index = roomBox?.participants.firstIndex(where: { $0.userId == userId })
        else { return }
        if let muted = muted { roomBox!.participants[index].muted = muted }
        if let deafened = deafened { roomBox!.participants[index].deafened = deafened }
    }

    // ---- 读面 ---------------------------------------------------------------

    public func room() -> RoomSnapshot? {
        lock.lock()
        defer { lock.unlock() }
        return roomBox
    }

    public func roomId() -> String? {
        lock.lock()
        defer { lock.unlock() }
        return roomBox?.roomId
    }
}
