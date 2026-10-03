import ChirpProtos
import ChirpProtocol
import Foundation

/// 壳层可见的服务事件。`emit` 在连接/调度线程上回调(协议包锁内),壳层
/// 自行跳主线程;**不得**在 emit 里同步回呼连接——与接收侧钩子同一契约。
public enum ChatServiceEvent {
    case connectionStateChanged(ConnState)
    case loginSucceeded
    case loginRejected(Chirp_Common_ErrorCode)
    case kicked(reason: String)
    case messageReceived(Chirp_Chat_ChatMessage)
    case offlineQueued(content: String)
    case offlineReplayed(count: Int)
    /// 反应计数变化(通知或自己的应答;数据在 service.reactions)。
    case reactionChanged(messageId: String)
    /// 有人开始/停止输入(数据在 service.typists)。
    case typing(channelId: String, userId: String, isTyping: Bool)
    /// 多端在线变化(登录清单或 notify 批次;数据在 service.devices)。
    case devicesChanged
    /// 好友名册变化(整表拉取或 request/accepted/removed notify;
    /// 数据在 service.friends)。
    case friendsChanged
    /// 对端在线状态变化(PRESENCE_NOTIFY 或 GET_PRESENCE 批量拉;
    /// 数据在 service.presence)。
    case presenceChanged
    /// 群名单变化(整表拉取、群 notify 或退群/被踢的本地移除;
    /// 数据在 service.groups)。
    case groupsChanged
    /// 一个会话已被移出(退群成功或自己被踢;数据面已清)。壳层据此关
    /// 打开中的聊天面并刷新列表。
    case sessionDropped(key: String, reason: SessionDropReason)
}

/// 会话被移出的原因(壳层文案分流)。
public enum SessionDropReason: Equatable {
    case left
    case kicked
}

/// 一次发送的终态(离线入队/拦截/服务端拒绝各有独立分支,壳层据此渲染)。
public enum SendOutcome {
    case sent
    case queuedOffline
    case blocked(String)
    case rejected(Chirp_Common_ErrorCode)
    case failed(String)
}

/// 登录+收发最小闭环的接线对象(蓝本 MainActivity.kt,UI-free 进包):
/// ChatConnection + ChatPipeline + MemoryMessageStore + WordFilterSync
/// (拦截器)+ OfflineSendQueue + MESSAGE_ACK 回执 + 会话索引。一次登录
 /// 一个实例——壳层每次登录新建,失败即弃;logout 幂等。
public final class ChatSessionService {
    public let userId: String
    private let deviceId: String

    private let conn: ChatConnection
    private let pipeline: ChatPipeline
    private let store: MemoryMessageStore
    private let sync: WordFilterSync
    private let queue: OfflineSendQueue
    private let emit: (ChatServiceEvent) -> Void
    private let now: () -> Int64
    private let lock = NSRecursiveLock()
    private var sendSeq = 0
    private var stateBox: ConnState = .idle
    /// 自有订阅(管线监听/回执/重放钩子)的退订令牌,logout 反注册。
    private var unsubs: [() -> Void] = []

    /// 壳层轮询用;事件流里的 connectionStateChanged 同步给它。
    public var connectionState: ConnState {
        lock.lock()
        defer { lock.unlock() }
        return stateBox
    }

    public init(
        userId: String,
        deviceId: String,
        chatUrl: String,
        transportFactory: @escaping (String) -> WsTransport,
        fallbackLexicon: String? = nil,
        scheduler: Scheduler = DispatchScheduler(),
        random: RandomSource = SystemRandomSource(),
        now: @escaping () -> Int64 = { Int64(Date().timeIntervalSince1970 * 1000) },
        emit: @escaping (ChatServiceEvent) -> Void
    ) {
        self.userId = userId
        self.deviceId = deviceId
        self.emit = emit
        self.now = now

        // 全部部件先以局部量成形(对象图内互引不带 self,满足确定初始化),
        // 再落属性、最后挂 self 参与的订阅。
        let connection = ChatConnection(
            url: chatUrl, transportFactory: transportFactory,
            random: random, scheduler: scheduler)
        let pipe = ChatPipeline(conn: connection, selfId: { userId }, deviceId: { deviceId })
        let archive = MemoryMessageStore()
        pipe.store = archive

        let filterSync = WordFilterSync(conn: connection)
        if let fallbackLexicon = fallbackLexicon {
            // 无文件=空词库放行,服务端仍强制自有过滤(词库下发批次语义)。
            filterSync.loadLocal(text: fallbackLexicon)
        }
        filterSync.start()
        pipe.interceptor = filterSync

        let index = SessionIndex(selfId: userId)
        let reactionIndex = ReactionIndex()
        let typingIndex = TypingIndex()
        let deviceIndex = OnlineDeviceIndex()
        let friendIndex = FriendIndex()
        let presenceIndex = PresenceIndex()
        let groupIndex = GroupIndex()
        // 队列重放不经过 service.send(),成功记账在这里补——会话预览对直发
        // 与重放一致(存档侧管线已落,索引侧是应用态)。按 options 的频道
        // 形态折键:私聊走接收方折 'p:' 键,群走频道 id 折 'g:' 键。
        let offlineQueue = OfflineSendQueue { options, content in
            pipe.send(options: options, content: content).map { response in
                if response.code == .ok {
                    if let peerId = options.receiverId, !peerId.isEmpty {
                        let channel = SessionChannel.dm(selfId: userId, peerId: peerId)
                        index.recordOutgoing(
                            key: channel.key, peerId: channel.peerId,
                            content: content, timestamp: now())
                    } else if options.channelType == .guild,
                        let groupId = options.channelId, !groupId.isEmpty
                    {
                        let channel = SessionChannel.group(groupId)
                        index.recordOutgoing(
                            key: channel.key, peerId: channel.peerId,
                            content: content, timestamp: now())
                    }
                }
                return response
            }
        }

        conn = connection
        pipeline = pipe
        store = archive
        sync = filterSync
        queue = offlineQueue
        sessions = index
        reactions = reactionIndex
        typists = typingIndex
        devices = deviceIndex
        friends = friendIndex
        presence = presenceIndex
        groups = groupIndex

        unsubs = [
            pipe.addListener(self),
            wireMessageAcks(),
            connection.onReconnected { [weak self] in self?.replayOfflineQueue() },
            wireReactionNotifies(reactions: reactionIndex, typists: typingIndex),
            wireDevicesNotifies(devices: deviceIndex),
            wireSocialNotifies(friends: friendIndex, presence: presenceIndex),
            wireGroupNotifies(),
        ]
    }

    /// start(订阅先于 connect,否则错过 .connecting/.connected 状态事件)
    /// → connect → 管线登录(dev 阶段用户名即 token)。终态经 onLoginResult
    /// 事件广播;这里只回传 code。
    public func login() -> Promise<Chirp_Common_ErrorCode> {
        pipeline.start()
        return conn.connect().flatMap { [pipeline, userId] in
            pipeline.login(userId: userId)
        }
    }

    /// 断开并退订;幂等,之后实例弃用。
    public func logout() {
        let tokens: [() -> Void] = {
            lock.lock()
            defer { lock.unlock() }
            let taken = unsubs
            unsubs = []
            return taken
        }()
        for unsubscribe in tokens { unsubscribe() }
        devices.reset()
        friends.reset()
        presence.reset()
        groups.reset()
        pipeline.stop()
        sync.stop()
        conn.disconnect()
    }

    // ---- 收发 ---------------------------------------------------------------

    /// 发 DM(send(channel:) 的私聊便捷面)。
    public func send(peerId: String, content: String) -> Promise<SendOutcome> {
        send(channel: .dm(selfId: userId, peerId: peerId), content: content)
    }

    /// 向任意会话频道发一条(DM/群共用的唯一发送入口)。断线(CLOSED)进
    /// 离线队列、重连后自动重放;返回终态 Promise(管线回调线程上 settle)。
    public func send(channel: SessionChannel, content: String) -> Promise<SendOutcome> {
        let options: SendOptions
        switch channel.kind {
        case .dm:
            options = SendOptions(channelType: .private, receiverId: channel.peerId)
        case .group:
            // 群发送只带频道,receiver 留空(chat_validation 对群的要求)。
            options = SendOptions(channelType: .guild, channelId: channel.channelId)
        }
        lock.lock()
        sendSeq += 1
        let seq = sendSeq
        lock.unlock()
        // 逐条唯一(进程内单调):队列的 clientId 去重只兜重复入队,不再像
        // 蓝本那样按内容哈希(同内容连发两条会被误判重)。
        let clientId = "\(userId)#\(seq)"
        return pipeline.send(options: options, content: content)
            .handle { [weak self] resp, err in
                guard let self = self else { return .failed("service released") }
                if let err = err {
                    if let requestError = err as? RequestError {
                        switch requestError.kind {
                        case .closed:
                            guard self.queue.enqueue(
                                clientId: clientId, options: options, content: content)
                            else {
                                return .failed("offline queue rejected the entry")
                            }
                            self.emit(.offlineQueued(content: content))
                            return .queuedOffline
                        case .blocked:
                            return .blocked(requestError.message ?? "blocked")
                        default:
                            return .failed(requestError.message ?? "send failed")
                        }
                    }
                    if let argumentError = err as? ChirpArgumentError {
                        return .failed(argumentError.message)
                    }
                    return .failed(String(describing: err))
                }
                guard let resp = resp else { return .failed("no response") }
                if resp.code == .ok {
                    self.sessions.recordOutgoing(
                        key: channel.key, peerId: channel.peerId,
                        content: content, timestamp: self.now())
                    return .sent
                }
                return .rejected(resp.code)
            }
    }

    /// 该频道的本地历史(管线 store,最新在前;含发送侧存档副本)。坏键
    /// (登出竞态/结构破损)返回空,不猜频道。
    public func history(key: String, limit: Int = 200) -> [Chirp_Chat_ChatMessage] {
        guard let channel = SessionChannel(key: key, selfId: userId) else { return [] }
        return pipeline.loadHistory(
            channelType: channel.channelType,
            channelId: channel.channelId,
            limit: limit)
    }

    /// 打开会话:store 面与索引面一起清未读。坏键 no-op。
    public func markRead(key: String) {
        guard let channel = SessionChannel(key: key, selfId: userId) else { return }
        pipeline.markRead(
            channelType: channel.channelType,
            channelId: channel.channelId)
        sessions.markRead(key: key)
    }

    /// 会话索引(壳层列表/未读的数据面)。
    public let sessions: SessionIndex
    /// 快捷反应与输入状态的数据面(P4a)。
    public let reactions: ReactionIndex
    public let typists: TypingIndex
    /// 多端在线数据面(P4b):登录清单 + DEVICES_PRESENCE_NOTIFY 同槽。
    public let devices: OnlineDeviceIndex
    /// 好友名册数据面(P4c):服务端权威,GET_FRIEND_LIST 整表 + 三条 notify。
    public let friends: FriendIndex
    /// 对端在线状态数据面(P4c):PRESENCE_NOTIFY 增量 + GET_PRESENCE 批量。
    public let presence: PresenceIndex
    /// 群名单数据面(P4e):服务端权威,GET_USER_GROUPS 整表 + 群 notify 重拉。
    public let groups: GroupIndex

    // ---- 反应/输入状态/服务端历史(P4a)--------------------------------------

    /// 给消息加一枚反应(请求 userID 必须是登录身份——服务端 SameUser 守卫)。
    /// ok 且带聚合时用聚合覆盖本地槽;失败原样透传 code。
    public func addReaction(
        messageId: String, emoji: String
    ) -> Promise<Chirp_Common_ErrorCode> {
        var request = Chirp_Chat_AddReactionRequest()
        request.messageID = messageId
        request.userID = userId
        request.emoji = emoji
        return conn.request(spec: MsgSpecs.addReaction, body: request)
            .map { [weak self] resp in
                if let self = self, resp.code == .ok, resp.hasReaction {
                    self.reactions.applyAggregate(
                        messageId: messageId, reaction: resp.reaction, selfId: self.userId)
                    self.emit(.reactionChanged(messageId: messageId))
                }
                return resp.code
            }
    }

    /// 撤自己的反应。RESP 不带聚合、notify 对操作者被排除——本地递减
    /// (web removeReaction 同款);失败原样透传 code。
    public func removeReaction(
        messageId: String, emoji: String
    ) -> Promise<Chirp_Common_ErrorCode> {
        var request = Chirp_Chat_RemoveReactionRequest()
        request.messageID = messageId
        request.userID = userId
        request.emoji = emoji
        return conn.request(spec: MsgSpecs.removeReaction, body: request)
            .map { [weak self] resp in
                if let self = self, resp.code == .ok {
                    self.reactions.record(
                        messageId: messageId, emoji: emoji,
                        userId: self.userId, added: false, selfId: self.userId)
                    self.emit(.reactionChanged(messageId: messageId))
                }
                return resp.code
            }
    }

    /// 上报输入状态(fire-and-forget,2208 无 REQ/RESP 配对;起始节流由
    /// 壳层做——web 同款 3s 首报间隔,发送/清空即补一条 stop)。频道类型
    /// 由调用方给——群输入指示要带 GUILD(web sendTyping 同款)。
    public func sendTyping(
        channelType: Chirp_Chat_ChannelType, channelId: String, isTyping: Bool
    ) {
        var indicator = Chirp_Chat_TypingIndicator()
        indicator.channelID = channelId
        indicator.channelType = channelType
        indicator.userID = userId
        // dev 阶段无昵称面,展示名=userId(web sendTyping 同款)。
        indicator.username = userId
        indicator.isTyping = isTyping
        indicator.timestamp = now()
        try? conn.send(msgId: .typingIndicatorNotify, body: [UInt8](indicator.serializedData()))
    }

    /// 拉服务端频道历史(跨设备/重装后的真相面;store 面只有本会话内存)。
    /// `beforeTimestamp` 传更早页最后一条的时间戳即向前翻页(0=最新一页,
    /// web loadHistory 同款);壳层按 messageID 去重合并进展示列表。
    /// 坏键(登出竞态/结构破损)直接失败,不猜频道。
    public func loadServerHistory(
        key: String, limit: Int32 = 50, beforeTimestamp: Int64 = 0
    ) -> Promise<Chirp_Chat_GetHistoryResponse> {
        guard let channel = SessionChannel(key: key, selfId: userId) else {
            return Promise.failed(ChirpArgumentError("bad session key"))
        }
        var request = Chirp_Chat_GetHistoryRequest()
        request.userID = userId
        request.channelType = channel.channelType
        request.channelID = channel.channelId
        request.beforeTimestamp = beforeTimestamp
        request.limit = limit
        return conn.request(spec: MsgSpecs.getHistory, body: request)
    }

    // ---- 内部接线 -----------------------------------------------------------

    /// 反应/输入状态的服务端通知(P4a,对齐 web 的纯 notify 驱动口径——
    /// 历史消息不回填反应)。坏 body 丢弃不伤链路。
    private func wireReactionNotifies(
        reactions: ReactionIndex, typists: TypingIndex
    ) -> () -> Void {
        let offAdded = conn.onNotify(msgId: .reactionAddedNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Chat_ReactionAddedNotify(serializedBytes: Data(body))
            else { return }
            reactions.record(
                messageId: notify.messageID, emoji: notify.emoji,
                userId: notify.userID, added: true, selfId: self.userId)
            self.emit(.reactionChanged(messageId: notify.messageID))
        }
        let offRemoved = conn.onNotify(msgId: .reactionRemovedNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Chat_ReactionRemovedNotify(serializedBytes: Data(body))
            else { return }
            reactions.record(
                messageId: notify.messageID, emoji: notify.emoji,
                userId: notify.userID, added: false, selfId: self.userId)
            self.emit(.reactionChanged(messageId: notify.messageID))
        }
        let offTyping = conn.onNotify(msgId: .typingIndicatorNotify) { [weak self] body in
            guard let self = self,
                let indicator = try? Chirp_Chat_TypingIndicator(serializedBytes: Data(body))
            else { return }
            typists.record(indicator, selfId: self.userId)
            self.emit(
                .typing(
                    channelId: indicator.channelID,
                    userId: indicator.userID,
                    isTyping: indicator.isTyping))
        }
        return {
            offAdded()
            offRemoved()
            offTyping()
        }
    }

    /// DEVICES_PRESENCE_NOTIFY → 在线索引(服务端推给本账号的其他在线
    /// 会话;登录清单在 onLoginDevices)。坏 body 丢弃不伤链路。
    private func wireDevicesNotifies(devices: OnlineDeviceIndex) -> () -> Void {
        conn.onNotify(msgId: .devicesPresenceNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Auth_DevicesPresenceNotify(serializedBytes: Data(body))
            else { return }
            devices.apply(notify.devices, nowMs: self.now())
            self.emit(.devicesChanged)
        }
    }

    /// PRESENCE_NOTIFY / FRIEND_*_NOTIFY → social 两个索引。服务端把
    /// PRESENCE_NOTIFY 广播给好友,FRIEND_REQUEST/ACCEPTED/REMOVED 定向发给
    /// 涉及方;坏 body 丢弃不伤链路。ACCEPTED 携带的是**对方** id(双方对称),
    /// 与 web 同款——本地接受后靠这条通知补名册,不在应答里写。
    private func wireSocialNotifies(
        friends: FriendIndex, presence: PresenceIndex
    ) -> () -> Void {
        let offPresence = conn.onNotify(msgId: .presenceNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Social_PresenceNotify(serializedBytes: Data(body))
            else { return }
            presence.set(
                notify.userID, status: notify.status,
                statusMessage: notify.statusMessage, atMs: self.now())
            self.emit(.presenceChanged)
        }
        let offRequest = conn.onNotify(msgId: .friendRequestNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Social_FriendRequestNotify(serializedBytes: Data(body))
            else { return }
            friends.addPendingIn(
                requestId: notify.requestID, fromUserId: notify.fromUserID)
            self.emit(.friendsChanged)
        }
        let offAccepted = conn.onNotify(msgId: .friendAcceptedNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Social_FriendAcceptedNotify(serializedBytes: Data(body))
            else { return }
            friends.addFriend(notify.userID)
            self.emit(.friendsChanged)
        }
        let offRemoved = conn.onNotify(msgId: .friendRemovedNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Social_FriendRemovedNotify(serializedBytes: Data(body))
            else { return }
            friends.removeFriend(notify.userID)
            // 名册已无此人,其 presence 快照一并作废(留着只会陈旧)。
            presence.remove(notify.userID)
            self.emit(.friendsChanged)
        }
        return {
            offPresence()
            offRequest()
            offAccepted()
            offRemoved()
        }
    }

    /// 群名单/成员变动的服务端通知(P4e,对齐 web 的 notify→重拉口径:
    /// JOINED/LEFT/UPDATED 整表重拉,2120 只有「被踢的是我」才本地双删)。
    /// 从 notify 里再发一条请求是安全的——连接锁是递归的,应答异步落位,
    /// web notify 处理器同款 fire-and-forget。坏 body 丢弃不伤链路。
    private func wireGroupNotifies() -> () -> Void {
        let offJoined = conn.onNotify(msgId: .groupMemberJoinedNotify) { [weak self] _ in
            _ = self?.refreshGroups()
        }
        let offLeft = conn.onNotify(msgId: .groupMemberLeftNotify) { [weak self] _ in
            _ = self?.refreshGroups()
        }
        let offUpdated = conn.onNotify(msgId: .groupUpdatedNotify) { [weak self] _ in
            _ = self?.refreshGroups()
        }
        let offKicked = conn.onNotify(msgId: .groupMemberKickedNotify) { [weak self] body in
            guard let self = self,
                let notify = try? Chirp_Chat_GroupMemberKickedNotify(serializedBytes: Data(body))
            else { return }
            guard notify.userID == self.userId else {
                // 别人被踢:名单走重拉(memberCount 变了)。
                _ = self.refreshGroups()
                return
            }
            // 自己被踢:服务端权威已移除,本地双删并发 dropped 让壳层关
            // 打开中的聊天面(web onKickedNotify 同款)。
            let key = SessionChannel.group(notify.groupID).key
            self.groups.remove(groupId: notify.groupID)
            self.sessions.remove(key: key)
            self.emit(.sessionDropped(key: key, reason: .kicked))
            self.emit(.groupsChanged)
        }
        return {
            offJoined()
            offLeft()
            offUpdated()
            offKicked()
        }
    }

    // ---- 好友/在线状态请求面(P4c,web social_api.ts 同款字段)-------------------

    /// 拉好友名册(整表换入)并顺带拉待处理申请;返回名册的 code。待处理
    /// 那一路失败静默——名册可用即面板可用,申请另走一条 .friendsChanged。
    @discardableResult
    public func refreshFriends() -> Promise<Chirp_Common_ErrorCode> {
        let pending = Chirp_Social_GetPendingRequestsRequest.with { $0.userID = userId }
        _ = conn.request(spec: MsgSpecs.getPendingRequests, body: pending)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return false }
                self.friends.replacePendingIn(
                    resp.requests.map {
                        .init(requestId: $0.requestID, fromUserId: $0.fromUserID)
                    })
                self.emit(.friendsChanged)
                return true
            }

        let list = Chirp_Social_GetFriendListRequest.with {
            $0.userID = userId
            $0.limit = 0  // 0 = 不分页,演示栈一次性取全量
            $0.offset = 0
        }
        return conn.request(spec: MsgSpecs.getFriendList, body: list)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return Chirp_Common_ErrorCode.internalError }
                self.friends.replaceFriends(resp.friends.map(\.userID))
                self.emit(.friendsChanged)
                return resp.code
            }
    }

    /// 批量拉对端在线状态(名册/会话列表进面板时调);空名单直接 ok,
    /// 不发包。
    @discardableResult
    public func pullPresence(of userIds: [String]) -> Promise<Chirp_Common_ErrorCode> {
        let ids = userIds.filter { !$0.isEmpty && $0 != userId }
        guard !ids.isEmpty else {
            return Promise<Chirp_Common_ErrorCode>.completed(.ok)
        }
        let request = Chirp_Social_GetPresenceRequest.with { $0.userIds = ids }
        return conn.request(spec: MsgSpecs.getPresence, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return Chirp_Common_ErrorCode.internalError }
                let atMs = self.now()
                for info in resp.presences {
                    self.presence.set(
                        info.userID, status: info.status,
                        statusMessage: info.statusMessage, atMs: atMs)
                }
                self.emit(.presenceChanged)
                return resp.code
            }
    }

    /// 改自己的在线状态(广播给好友)。statusMessage 空串即清空。
    @discardableResult
    public func setPresence(
        _ status: Chirp_Social_PresenceStatus, statusMessage: String = ""
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Social_SetPresenceRequest.with {
            $0.userID = userId
            $0.status = status
            $0.statusMessage = statusMessage
        }
        return conn.request(spec: MsgSpecs.setPresence, body: request)
            .map { resp in resp.code }
    }

    /// 发起好友申请。成功只记本地出向(对方是否收到由服务端 notify 决定),
    /// 与 web 同款。
    @discardableResult
    public func addFriend(
        targetUserId: String, message: String = ""
    ) -> Promise<Chirp_Common_ErrorCode> {
        guard !targetUserId.isEmpty, targetUserId != userId else {
            return Promise<Chirp_Common_ErrorCode>.completed(.invalidParam)
        }
        let request = Chirp_Social_AddFriendRequest.with {
            $0.userID = userId
            $0.targetUserID = targetUserId
            $0.message = message
        }
        return conn.request(spec: MsgSpecs.addFriend, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                self.friends.addPendingOut(targetUserId)
                self.emit(.friendsChanged)
                return resp.code
            }
    }

    /// 同意/拒绝一条入向申请。应答成功后出队;名册由 ACCEPTED notify 补。
    @discardableResult
    public func respondToRequest(
        requestId: String, accept: Bool
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Social_FriendRequestAction.with {
            $0.userID = userId
            $0.requestID = requestId
            $0.accept = accept
        }
        return conn.request(spec: MsgSpecs.friendRequestAction, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                self.friends.resolvePending(requestId: requestId)
                self.emit(.friendsChanged)
                return resp.code
            }
    }

    /// 删好友(服务端对称幂等);本地同步移除,对端走 notify。
    @discardableResult
    public func removeFriend(
        targetUserId: String
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Social_RemoveFriendRequest.with {
            $0.userID = userId
            $0.friendUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.removeFriend, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                self.friends.removeFriend(targetUserId)
                self.emit(.friendsChanged)
                return resp.code
            }
    }

    // ---- 群请求面(P4e,web chat_api.ts 同款字段)---------------------------

    /// 拉群名单(整表换入)并给每个群落无预览的会话行;返回名册 code。
    /// web refreshGroups 同款:名单先到,预览由消息记账填。
    @discardableResult
    public func refreshGroups() -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Chat_GetUserGroupsRequest.with {
            $0.userID = userId
            $0.limit = 0  // 0 = 不分页,演示栈一次性取全量
            $0.offset = 0
        }
        return conn.request(spec: MsgSpecs.getUserGroups, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return Chirp_Common_ErrorCode.internalError }
                let entries = resp.groups.map { GroupIndex.Entry(info: $0) }
                self.groups.replace(entries)
                for entry in entries { self.sessions.ensureGroup(groupId: entry.groupId) }
                // 整表换入后修剪已不在名单的群行——行随名单走(web 会话行派生
                // 自 group store 同款):被移出/解散的群不留幽灵行(未读一并)。
                let liveGroupIds = Set(entries.map(\.groupId))
                for summary in self.sessions.summaries()
                where summary.kind == .group && !liveGroupIds.contains(summary.peerId) {
                    self.sessions.remove(key: summary.key)
                }
                self.emit(.groupsChanged)
                return resp.code
            }
    }

    /// 建群;成功再刷一次名单(web createGroup 同序)。回群 id——失败
    /// (含服务端拒绝)回空串,壳层据此给「建群失败」文案。
    public func createGroup(name: String, description: String = "") -> Promise<String> {
        guard !name.isEmpty else { return Promise.completed("") }
        let request = Chirp_Chat_CreateGroupRequest.with {
            $0.creatorID = userId
            $0.groupName = name
            $0.description_p = description
        }
        return conn.request(spec: MsgSpecs.createGroup, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else { return "" }
                _ = self.refreshGroups()
                return resp.groupID
            }
    }

    /// 邀人进群;成功刷名单(成员数/名单随 notify 双路都到,web 同款)。
    @discardableResult
    public func inviteToGroup(
        groupId: String, targetUserId: String
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Chat_InviteToGroupRequest.with {
            $0.inviterID = userId
            $0.groupID = groupId
            $0.targetUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.inviteToGroup, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                _ = self.refreshGroups()
                return resp.code
            }
    }

    /// 群主移出成员(服务端校验 owner);成功刷名单。移出自己不做——
    /// 被踢走 2120 notify 路径。
    @discardableResult
    public func kickMember(
        groupId: String, targetUserId: String
    ) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Chat_KickMemberRequest.with {
            $0.requesterID = userId
            $0.groupID = groupId
            $0.targetUserID = targetUserId
        }
        return conn.request(spec: MsgSpecs.kickMember, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                _ = self.refreshGroups()
                return resp.code
            }
    }

    /// 退群。服务端对 actor 不发 MEMBER_LEFT notify(操作者自己知道),
    /// 成功即本地双删:名单 + 会话行,并发 sessionDropped(.left) 让壳层
    /// 关打开中的聊天面(web leaveGroup 同款本地清)。
    @discardableResult
    public func leaveGroup(groupId: String) -> Promise<Chirp_Common_ErrorCode> {
        let request = Chirp_Chat_LeaveGroupRequest.with {
            $0.userID = userId
            $0.groupID = groupId
        }
        return conn.request(spec: MsgSpecs.leaveGroup, body: request)
            .handle { [weak self] resp, err in
                guard let self = self, err == nil, let resp = resp, resp.code == .ok
                else {
                    return err == nil
                        ? (resp?.code ?? Chirp_Common_ErrorCode.internalError)
                        : Chirp_Common_ErrorCode.internalError
                }
                let key = SessionChannel.group(groupId).key
                self.groups.remove(groupId: groupId)
                self.sessions.remove(key: key)
                self.emit(.sessionDropped(key: key, reason: .left))
                self.emit(.groupsChanged)
                return resp.code
            }
    }

    /// 群成员名单(面板用;0=不分页同 web)。返回原始应答——壳层按
    /// code 分流文案。
    public func loadGroupMembers(
        groupId: String
    ) -> Promise<Chirp_Chat_GetGroupMembersResponse> {
        let request = Chirp_Chat_GetGroupMembersRequest.with {
            $0.groupID = groupId
            $0.limit = 0
            $0.offset = 0
        }
        return conn.request(spec: MsgSpecs.getGroupMembers, body: request)
    }

    /// CHAT_MESSAGE_NOTIFY → MESSAGE_ACK(服务端 10s 收不到回执会把投递
    /// 回滚进离线滞留)。本订阅先于管线的渲染订阅注册,故回执先于渲染——
    /// 与蓝本 wireMessageAcks 同序。坏 body 丢弃不伤链路。
    private func wireMessageAcks() -> () -> Void {
        conn.onNotify(msgId: .chatMessageNotify) { [weak self] body in
            guard let self = self else { return }
            guard let message = try? Chirp_Chat_ChatMessage(serializedBytes: Data(body))
            else { return }
            var ack = Chirp_Chat_MessageAck()
            ack.messageID = message.messageID
            ack.userID = self.userId
            ack.receivedAt = self.now()
            try? self.conn.send(msgId: .messageAck, body: [UInt8](ack.serializedData()))
        }
    }

    private func replayOfflineQueue() {
        guard queue.size() > 0 else { return }
        queue.flush().onComplete { [weak self] outcome in
            if case .success(let confirmed) = outcome, confirmed > 0 {
                self?.emit(.offlineReplayed(count: confirmed))
            }
        }
    }
}

extension ChatSessionService: ChatEventListener {
    public func onConnectionStateChanged(_ state: ConnState) throws {
        lock.lock()
        stateBox = state
        lock.unlock()
        emit(.connectionStateChanged(state))
    }

    public func onLoginResult(_ code: Chirp_Common_ErrorCode, userId: String) throws {
        if code == .ok {
            // 词库下发:登录成功即拉一次;失败静默,回退词库继续生效,
            // 后续热更走 UPDATE_NOTIFY(sync 已 start)。
            _ = sync.fetch()
            // 好友名册 + 在线状态:登录即整表拉一次(与 web friend_store
            // 同款)。名册先到再按名册批量拉 presence;两路失败静默——
            // 面板不可用不该影响登录终态,notify 后续会补。
            _ = refreshFriends().flatMap { [weak self] _ -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self else { return Promise.completed(.ok) }
                return self.pullPresence(of: self.friends.friends())
            }
            // 群名单引导:登录即整表拉一次——否则重登丢群(web ChatPage 挂载
            // 同款;只靠建群/受邀 notify 会漏)。失败静默,notify 后续会补。
            _ = refreshGroups()
            emit(.loginSucceeded)
        } else {
            emit(.loginRejected(code))
        }
    }

    public func onKicked(reason: String) throws {
        emit(.kicked(reason: reason))
    }

    public func onMessageReceived(_ message: Chirp_Chat_ChatMessage) throws {
        sessions.recordIncoming(message)
        emit(.messageReceived(message))
    }

    /// 登录响应的初始清单(服务端已排除本会话):入索引,与 notify 同槽。
    public func onLoginDevices(_ devices: [Chirp_Auth_DevicePresence]) throws {
        guard !devices.isEmpty else { return }
        self.devices.apply(devices, nowMs: now())
        emit(.devicesChanged)
    }
}
