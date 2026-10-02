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

        unsubs = [
            pipe.addListener(self),
            wireMessageAcks(),
            connection.onReconnected { [weak self] in self?.replayOfflineQueue() },
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

    // ---- 内部接线 -----------------------------------------------------------

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
}
