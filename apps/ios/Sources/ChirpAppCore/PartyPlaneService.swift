import ChirpProtos
import ChirpProtocol
import Foundation

/// 组队面服务(P4d,web PartyApi 的 Swift 对位):第三条 WS(party 面
/// 7501),跨游戏组队。降级同 social/party 惯例——socket 不可用时聊天
/// 照常、组队功能隐藏。同步是快照式的:服务端每次变更把全量 PartyInfo
/// 发给每个成员(含操作者),镜像只 apply 不 diff。
public final class PartyPlaneService {
    public enum Event {
        /// 登录+初始 GET_MY_PARTY 完成(面可用)。
        case loggedIn
        case rejected(Chirp_Common_ErrorCode)
        case failed(String)
        /// 快照或邀请队列变化(数据在 service.party)。
        case changed
    }

    public let userId: String
    public let party: PartyIndex
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
        partyUrl: String,
        transportFactory: @escaping (String) -> WsTransport,
        scheduler: Scheduler = DispatchScheduler(),
        random: RandomSource = SystemRandomSource(),
        emit: @escaping (Event) -> Void
    ) {
        self.userId = userId
        party = PartyIndex()
        conn = ChatConnection(
            url: partyUrl, transportFactory: transportFactory,
            random: random, scheduler: scheduler)
        self.emit = emit
        _ = conn.onStateChange { [weak self] state in
            guard let self = self else { return }
            self.lock.lock()
            self.stateBox = state
            self.lock.unlock()
        }
    }

    /// connect → 面登录(dev 用户名即 token)→ notify 先上 → 恢复在队
    /// 状态(GET_MY_PARTY)。非 ok 断链静默降级,终态只回传 code。
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
            // 换号不得泄漏上一账号的镜像;notify 先于拉取,竞态不丢事件。
            self.party.clear()
            self.party.resetInvites()
            self.wireNotifies()
            return self.fetchMyParty().map { _ in
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
        party.clear()
        party.resetInvites()
    }

    // ---- 请求面(web party_api.ts 同款字段) -----------------------------------

    /// 重拉当前在队状态(重连/登录竞态恢复)。
    @discardableResult
    public func fetchMyParty() -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Party_GetMyPartyRequest.with { $0.userID = userId }
        return conn.request(spec: MsgSpecs.getMyParty, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return Chirp_Common_ErrorCode.internalError
                }
                if resp.code == .ok {
                    if resp.inParty, resp.hasParty {
                        self.party.apply(Self.snapshot(of: resp.party))
                    } else {
                        self.party.clear()
                    }
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    @discardableResult
    public func createParty(maxMembers: Int32 = 0) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Party_CreatePartyRequest.with {
            $0.userID = userId
            $0.maxMembers = maxMembers
        }
        return conn.request(spec: MsgSpecs.createParty, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return Chirp_Common_ErrorCode.internalError
                }
                if resp.code == .ok, resp.hasParty {
                    self.party.apply(Self.snapshot(of: resp.party))
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    @discardableResult
    public func invite(targetUserId: String) -> Promise<Chirp_Common_ErrorCode> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Party_InviteToPartyRequest.with {
            $0.userID = userId
            $0.partyID = partyId
            $0.targetUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.inviteToParty, body: request).map { $0.code }
    }

    @discardableResult
    public func acceptInvite(inviteId: String) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Party_AcceptInviteRequest.with {
            $0.userID = userId
            $0.inviteID = inviteId
        }
        return conn.request(spec: MsgSpecs.acceptPartyInvite, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp else {
                    return Chirp_Common_ErrorCode.internalError
                }
                if resp.code == .ok {
                    self.party.removeInvite(inviteId: inviteId)
                    if resp.hasParty {
                        self.party.apply(Self.snapshot(of: resp.party))
                    }
                    self.emit(.changed)
                }
                return resp.code
            }
    }

    @discardableResult
    public func declineInvite(inviteId: String) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Party_DeclineInviteRequest.with {
            $0.userID = userId
            $0.inviteID = inviteId
        }
        return conn.request(spec: MsgSpecs.declinePartyInvite, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return Chirp_Common_ErrorCode.internalError
                }
                self.party.removeInvite(inviteId: inviteId)
                self.emit(.changed)
                return resp.code
            }
    }

    /// 退队;返回值=服务端是否顺带解散(最后一人离开)。
    public func leaveParty() -> Promise<Bool> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Bool>.completed(false)
        }
        let request = Chirp_Party_LeavePartyRequest.with {
            $0.userID = userId
            $0.partyID = partyId
        }
        return conn.request(spec: MsgSpecs.leaveParty, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return false }
                self.party.clear()
                self.emit(.changed)
                return resp.partyDisbanded
            }
    }

    /// 仅队长;kicked 成员从 PARTY_KICKED_NOTIFY 得知。
    @discardableResult
    public func kickMember(targetUserId: String) -> Promise<Chirp_Common_ErrorCode> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Party_KickMemberRequest.with {
            $0.userID = userId
            $0.partyID = partyId
            $0.targetUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.kickPartyMember, body: request).map { $0.code }
    }

    /// 仅队长;解散,成员从 PARTY_DISBANDED_NOTIFY 得知。
    @discardableResult
    public func disbandParty() -> Promise<Chirp_Common_ErrorCode> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Party_DisbandPartyRequest.with {
            $0.userID = userId
            $0.partyID = partyId
        }
        return conn.request(spec: MsgSpecs.disbandParty, body: request).map { $0.code }
    }

    /// 仅队长;队长移交。
    @discardableResult
    public func transferLeader(targetUserId: String) -> Promise<Chirp_Common_ErrorCode> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Party_TransferLeaderRequest.with {
            $0.userID = userId
            $0.partyID = partyId
            $0.targetUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.transferPartyLeader, body: request).map { $0.code }
    }

    @discardableResult
    public func setReady(_ ready: Bool) -> Promise<Chirp_Common_ErrorCode> {
        guard let partyId = party.party()?.partyId, !partyId.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Party_SetReadyRequest.with {
            $0.userID = userId
            $0.partyID = partyId
            $0.ready = ready
        }
        return conn.request(spec: MsgSpecs.setPartyReady, body: request).map { $0.code }
    }

    // ---- 内部 -----------------------------------------------------------------

    private func wireNotifies() {
        let tokens = [
            conn.onNotify(msgId: .inviteNotify) { [weak self] body in
                guard let self = self,
                    let notify = try? Chirp_Party_InviteNotify(serializedBytes: Data(body))
                else { return }
                self.party.addInvite(
                    .init(
                        inviteId: notify.inviteID,
                        fromUserId: notify.fromUserID,
                        partyId: notify.party.partyID))
                self.emit(.changed)
            },
            conn.onNotify(msgId: .partyStateChangedNotify) { [weak self] body in
                guard let self = self,
                    let notify = try? Chirp_Party_PartyStateChangedNotify(
                        serializedBytes: Data(body)),
                    notify.hasParty
                else { return }
                self.party.apply(Self.snapshot(of: notify.party))
                self.emit(.changed)
            },
            // 被踢者已非成员、收不到快照;解码只做格式门(注释同 web)。
            conn.onNotify(msgId: .partyKickedNotify) { [weak self] body in
                guard let self = self,
                    (try? Chirp_Party_PartyKickedNotify(serializedBytes: Data(body))) != nil
                else { return }
                self.party.clear()
                self.emit(.changed)
            },
            conn.onNotify(msgId: .partyDisbandedNotify) { [weak self] body in
                guard let self = self,
                    (try? Chirp_Party_PartyDisbandedNotify(serializedBytes: Data(body))) != nil
                else { return }
                self.party.clear()
                self.emit(.changed)
            },
        ]
        lock.lock()
        unsubs = tokens
        lock.unlock()
    }

    private static func snapshot(of info: Chirp_Party_PartyInfo) -> PartyIndex.Snapshot {
        PartyIndex.Snapshot(
            partyId: info.partyID,
            leaderId: info.leaderID,
            maxMembers: info.maxMembers,
            members: info.members.map {
                .init(userId: $0.userID, ready: $0.ready)
            })
    }
}
