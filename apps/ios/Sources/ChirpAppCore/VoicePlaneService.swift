import ChirpProtos
import ChirpProtocol
import Foundation

/// 语音房间服务(P4d,web VoiceApi 的 Swift 对位):第五条 WS(voice 面
/// 9001),房间生命周期与 roster 同步。边界同 web:只有信令/名册面——
/// SDP/ICE 中继(4007-4009)与 SPEAKING_NOTIFY(4021)属 WebRTC 媒体面,
/// 伴侣 App 不带媒体;join 发空 sdp_offer,房间 UI 渲染 roster 状态。
public final class VoicePlaneService {
    public enum Event {
        /// 登录+房间恢复完成(面可用)。
        case loggedIn
        case rejected(Chirp_Common_ErrorCode)
        case failed(String)
        /// 房间镜像变化(数据在 service.room)。
        case changed
    }

    public let userId: String
    public let roomIndex: VoiceIndex
    private let conn: ChatConnection
    private let emit: (Event) -> Void
    private let lock = NSLock()
    private var unsubs: [() -> Void] = []
    private var stateBox: ConnState = .idle

    public var connectionState: ConnState {
        lock.lock()
        defer { lock.unlock() }
        return stateBox
    }

    public init(
        userId: String,
        voiceUrl: String,
        transportFactory: @escaping (String) -> WsTransport,
        scheduler: Scheduler = DispatchScheduler(),
        random: RandomSource = SystemRandomSource(),
        emit: @escaping (Event) -> Void
    ) {
        self.userId = userId
        roomIndex = VoiceIndex()
        conn = ChatConnection(
            url: voiceUrl, transportFactory: transportFactory,
            random: random, scheduler: scheduler)
        self.emit = emit
        _ = conn.onStateChange { [weak self] state in
            guard let self = self else { return }
            self.lock.lock()
            self.stateBox = state
            self.lock.unlock()
        }
    }

    /// connect → 面登录 → notify 先上 → 重新认领在房状态(GET_USER_ROOM)。
    public func login() -> Promise<Chirp_Common_ErrorCode> {
        conn.connect().flatMap { [weak self] in
            guard let self = self else {
                return Promise<Chirp_Common_ErrorCode>.failed(
                    RequestError(.closed, message: "service released"))
            }
            var request = Chirp_Auth_LoginRequest()
            request.token = self.userId
            request.platform = "ios"
            return self.conn.request(spec: MsgSpecs.login, body: request).map { $0.code }
        }
        .flatMap { [weak self] code -> Promise<Chirp_Common_ErrorCode> in
            guard let self = self else {
                return Promise<Chirp_Common_ErrorCode>.completed(code)
            }
            guard code == .ok else {
                self.conn.disconnect()
                self.emit(.rejected(code))
                return Promise<Chirp_Common_ErrorCode>.completed(code)
            }
            self.conn.resetBackoff()
            self.roomIndex.clear()
            self.wireNotifies()
            return self.restoreRoom().map { _ in
                self.emit(.loggedIn)
                return code
            }
        }
    }

    /// 断开并退订;幂等。
    public func shutdown() {
        let tokens: [() -> Void] = {
            lock.lock()
            defer { lock.unlock() }
            let taken = unsubs
            unsubs = []
            return taken
        }()
        for unsubscribe in tokens { unsubscribe() }
        conn.disconnect()
        roomIndex.clear()
    }

    // ---- 请求面(web voice_api.ts 同款字段) -----------------------------------

    /// 重新认领当前房间(重连/登录竞态恢复);不在房即清空镜像。
    @discardableResult
    public func restoreRoom() -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Voice_GetUserRoomRequest.with { $0.userID = userId }
        return conn.request(spec: MsgSpecs.getUserRoom, body: request)
            .flatMap { [weak self] resp -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self else {
                    return Promise<Chirp_Common_ErrorCode>.completed(resp.code)
                }
                guard resp.code == .ok, !resp.roomID.isEmpty else {
                    self.roomIndex.clear()
                    return Promise<Chirp_Common_ErrorCode>.completed(resp.code)
                }
                return self.refreshRoom(roomId: resp.roomID)
            }
    }

    @discardableResult
    public func createRoom(
        roomName: String = "", maxParticipants: Int32 = 0
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Voice_CreateRoomRequest.with {
            $0.userID = userId
            $0.roomType = .group
            $0.roomName = roomName
            $0.maxParticipants = maxParticipants
        }
        return conn.request(spec: MsgSpecs.createRoom, body: request)
            .flatMap { [weak self] resp -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self, resp.code == .ok else {
                    return Promise<Chirp_Common_ErrorCode>.completed(resp.code)
                }
                return self.refreshRoom(roomId: resp.roomID)
            }
    }

    /// 空 sdp_offer 会原样回显;媒体协商不在本面范围。
    @discardableResult
    public func joinRoom(roomId: String) -> Promise<Chirp_Common_ErrorCode> {
        guard !roomId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Voice_JoinRoomRequest.with {
            $0.userID = userId
            $0.roomID = roomId
            $0.sdpOffer = ""
        }
        return conn.request(spec: MsgSpecs.joinRoom, body: request)
            .flatMap { [weak self] resp -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self, resp.code == .ok else {
                    return Promise<Chirp_Common_ErrorCode>.completed(resp.code)
                }
                // JOIN_ROOM_RESP 只有 id;roster 细节靠整表拉取。
                return self.refreshRoom(roomId: roomId)
            }
    }

    public func leaveRoom() -> Promise<Bool> {
        guard let roomId = roomIndex.roomId(), !roomId.isEmpty else {
            return Promise<Bool>.completed(false)
        }
        let request = Chirp_Voice_LeaveRoomRequest.with {
            $0.userID = userId
            $0.roomID = roomId
        }
        return conn.request(spec: MsgSpecs.leaveRoom, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return false }
                self.roomIndex.clear()
                self.emit(.changed)
                return true
            }
    }

    /// 应答 ok 后本地回显——state 广播排除操作者(web 同款),标志细节
    /// 只能自己补。
    @discardableResult
    public func setMute(_ muted: Bool) -> Promise<Chirp_Common_ErrorCode> {
        guard let roomId = roomIndex.roomId(), !roomId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Voice_SetMuteRequest.with {
            $0.userID = userId
            $0.roomID = roomId
            $0.muted = muted
        }
        return conn.request(spec: MsgSpecs.setMute, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return resp?.code ?? .internalError
                }
                if resp.code == .ok {
                    self.roomIndex.applySelfFlags(userId: self.userId, muted: muted)
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    @discardableResult
    public func setDeafen(_ deafened: Bool) -> Promise<Chirp_Common_ErrorCode> {
        guard let roomId = roomIndex.roomId(), !roomId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Voice_SetDeafenRequest.with {
            $0.userID = userId
            $0.roomID = roomId
            $0.deafened = deafened
        }
        return conn.request(spec: MsgSpecs.setDeafen, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return resp?.code ?? .internalError
                }
                if resp.code == .ok {
                    self.roomIndex.applySelfFlags(userId: self.userId, deafened: deafened)
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    /// 整 roster 拉取;muted/deafened 细节的唯一来源。
    @discardableResult
    public func refreshRoom(roomId: String) -> Promise<Chirp_Common_ErrorCode> {
        guard !roomId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Voice_GetRoomInfoRequest.with { $0.roomID = roomId }
        return conn.request(spec: MsgSpecs.getRoomInfo, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return Chirp_Common_ErrorCode.internalError
                }
                if resp.code == .ok {
                    self.roomIndex.apply(
                        VoiceIndex.RoomSnapshot(
                            roomId: resp.roomID,
                            roomName: resp.roomName,
                            maxParticipants: resp.maxParticipants,
                            participants: resp.participants.map {
                                .init(
                                    userId: $0.userID, state: $0.state,
                                    muted: $0.muted, deafened: $0.deafened)
                            }))
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    // ---- 内部 -----------------------------------------------------------------

    private func wireNotifies() {
        let tokens = [
            conn.onNotify(msgId: .participantJoinedNotify) { [weak self] body in
                guard let self = self,
                    let notify = try? Chirp_Voice_ParticipantJoinedNotify(
                        serializedBytes: Data(body)),
                    notify.roomID == self.roomIndex.roomId(),
                    notify.hasParticipant
                else { return }
                self.roomIndex.upsert(
                    .init(
                        userId: notify.participant.userID,
                        state: notify.participant.state,
                        muted: notify.participant.muted,
                        deafened: notify.participant.deafened))
                self.emit(.changed)
            },
            conn.onNotify(msgId: .participantLeftNotify) { [weak self] body in
                guard let self = self,
                    let notify = try? Chirp_Voice_ParticipantLeftNotify(
                        serializedBytes: Data(body))
                else { return }
                self.roomIndex.removeParticipant(
                    roomId: notify.roomID, userId: notify.userID)
                self.emit(.changed)
            },
            conn.onNotify(msgId: .participantStateChangedNotify) { [weak self] body in
                guard let self = self,
                    let notify = try? Chirp_Voice_ParticipantStateChangedNotify(
                        serializedBytes: Data(body))
                else { return }
                self.roomIndex.updateState(
                    roomId: notify.roomID, userId: notify.userID,
                    state: notify.state)
                self.emit(.changed)
            },
            // SPEAKING_NOTIFY / ICE_CANDIDATE / SDP_OFFER / SDP_ANSWER 是
            // 媒体面(类注释):到达的帧一律不订阅。
        ]
        lock.lock()
        unsubs = tokens
        lock.unlock()
    }
}
