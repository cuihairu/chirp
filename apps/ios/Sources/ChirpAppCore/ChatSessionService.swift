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

        let index = SessionIndex()
        let reactionIndex = ReactionIndex()
        let typingIndex = TypingIndex()
        let deviceIndex = OnlineDeviceIndex()
        // 队列重放不经过 service.send(),成功记账在这里补——会话预览对直发
        // 与重放一致(存档侧管线已落,索引侧是应用态)。
        let offlineQueue = OfflineSendQueue { options, content in
            pipe.send(options: options, content: content).map { response in
                if response.code == .ok, let peerId = options.receiverId, !peerId.isEmpty {
                    index.recordOutgoing(peerId: peerId, content: content, timestamp: now())
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

        unsubs = [
            pipe.addListener(self),
            wireMessageAcks(),
            connection.onReconnected { [weak self] in self?.replayOfflineQueue() },
            wireReactionNotifies(reactions: reactionIndex, typists: typingIndex),
            wireDevicesNotifies(devices: deviceIndex),
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
        pipeline.stop()
        sync.stop()
        conn.disconnect()
    }

    // ---- 收发 ---------------------------------------------------------------

    /// 发 DM。断线(CLOSED)进离线队列、重连后自动重放;返回终态 Promise
    /// (管线回调线程上 settle)。
    public func send(peerId: String, content: String) -> Promise<SendOutcome> {
        let options = SendOptions(channelType: .private, receiverId: peerId)
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
                        peerId: peerId, content: content, timestamp: self.now())
                    return .sent
                }
                return .rejected(resp.code)
            }
    }

    /// 该对端的 DM 历史(管线 store,最新在前;含发送侧存档副本)。
    public func history(peerId: String, limit: Int = 200) -> [Chirp_Chat_ChatMessage] {
        pipeline.loadHistory(
            channelType: .private,
            channelId: SessionIndex.dmChannelId(userId, peerId),
            limit: limit)
    }

    /// 打开会话:store 面与索引面一起清未读。
    public func markRead(peerId: String) {
        pipeline.markRead(
            channelType: .private,
            channelId: SessionIndex.dmChannelId(userId, peerId))
        sessions.markRead(peerId: peerId)
    }

    /// 会话索引(壳层列表/未读的数据面)。
    public let sessions: SessionIndex
    /// 快捷反应与输入状态的数据面(P4a)。
    public let reactions: ReactionIndex
    public let typists: TypingIndex
    /// 多端在线数据面(P4b):登录清单 + DEVICES_PRESENCE_NOTIFY 同槽。
    public let devices: OnlineDeviceIndex

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
    /// 壳层做——web 同款 3s 首报间隔,发送/清空即补一条 stop)。
    public func sendTyping(channelId: String, isTyping: Bool) {
        var indicator = Chirp_Chat_TypingIndicator()
        indicator.channelID = channelId
        indicator.channelType = .private
        indicator.userID = userId
        // dev 阶段无昵称面,展示名=userId(web sendTyping 同款)。
        indicator.username = userId
        indicator.isTyping = isTyping
        indicator.timestamp = now()
        try? conn.send(msgId: .typingIndicatorNotify, body: [UInt8](indicator.serializedData()))
    }

    /// 拉服务端 DM 历史(跨设备/重装后的真相面;store 面只有本会话内存)。
    /// 壳层按 messageID 去重合并进展示列表。
    public func loadServerHistory(
        peerId: String, limit: Int32 = 50
    ) -> Promise<Chirp_Chat_GetHistoryResponse> {
        var request = Chirp_Chat_GetHistoryRequest()
        request.userID = userId
        request.channelType = .private
        request.channelID = SessionIndex.dmChannelId(userId, peerId)
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
