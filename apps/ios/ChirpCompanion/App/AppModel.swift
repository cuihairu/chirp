import ChirpAppCore
import ChirpProtos
import ChirpProtocol
import SwiftUI
import UIKit
import UserNotifications

/// 壳层状态机(P2:真接线)。登录→会话列表→DM 收发全走 ChatSessionService;
/// 服务事件在连接线程回调,统一 Task { @MainActor } 跳主线程再落 @Published。
@MainActor
final class AppModel: ObservableObject {
    @Published var draft = LoginDraft()
    @Published private(set) var phase: AppPhase = .loggedOut
    /// 连接横幅:断线重连/被踢等瞬态;nil = 无事。
    @Published var connectionBanner: String?
    /// 瞬态提示(离线入队/重放/发送失败),视图侧展示后清空。
    @Published var toast: String?
    @Published private(set) var sessionSummaries: [SessionIndex.Summary] = []
    /// 当前打开的会话消息(旧→新,渲染顺序)。
    @Published private(set) var chatMessages: [Chirp_Chat_ChatMessage] = []
    /// 当前展示消息的反应快照(P4a:messageId → tallies;事件驱动刷新)。
    @Published private(set) var reactionSummaries: [String: [ReactionIndex.Tally]] = [:]
    /// 当前会话 TTL 内在输入的对端(P4a;6s 过期由清扫任务兜底)。
    @Published private(set) var typingPeers: [String] = []
    private(set) var activePeer: String?

    /// typing 上报节流(web ChatWindow 同款:start ≥3s 一次、最后键击 5s
    /// 后自动 stop、发送即 stop、空串不触发)。MainActor 上串行,无锁。
    private var typingLastStartAt: Date?
    private var typingStopTask: Task<Void, Never>?
    private var typingSweepTask: Task<Void, Never>?

    /// 设备识别(首次生成即持久化);登录面展示用。
    let deviceId: String
    private var service: ChatSessionService?
    /// 设备面(app_gateway 5201)注册服务与本次登录用的 host 快照。
    private var devicePlane: DevicePlaneService?
    private var loginHost: HostConfig?

    init() {
        // UserDefaults 闭包对接 DeviceIdentity(key 与 web/android 同位)。
        let identity = DeviceIdentity(
            load: { UserDefaults.standard.string(forKey: Self.deviceIdKey) },
            save: { UserDefaults.standard.set($0, forKey: Self.deviceIdKey) })
        deviceId = identity.ensureDeviceId()
    }

    // ---- 登录/登出 ------------------------------------------------------------

    func loginTapped() {
        guard phase == .loggedOut, draft.isValid,
            let host = HostConfig.resolve(draft.host)
        else { return }
        let userId = draft.normalizedUserId
        loginHost = host
        connectionBanner = "连接中…"
        // 一次登录一个服务实例(蓝本同款:失败即弃,下次登录新建)。
        let service = ChatSessionService(
            userId: userId,
            deviceId: deviceId,
            chatUrl: host.chatUrl.absoluteString,
            transportFactory: { DarwinWsTransport(url: $0) },
            emit: { [weak self] event in
                Task { @MainActor in self?.handle(event) }
            })
        self.service = service
        service.login().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self, self.service === service else { return }
                // 终态正常路径经 .loginSucceeded/.loginRejected 事件落位;
                // 这里只兜连接层失败(open 失败/超时等 err)。
                if case .failure(let err) = outcome {
                    self.tearDownService()
                    self.connectionBanner = nil
                    self.toast = "连接失败:\(err.localizedDescription)"
                }
            }
        }
    }

    func logoutTapped() {
        tearDownService()
    }

    private func tearDownService() {
        service?.logout()
        service = nil
        devicePlane?.shutdown()
        devicePlane = nil
        AppDelegate.tokenSink = nil
        phase = .loggedOut
        activePeer = nil
        chatMessages = []
        sessionSummaries = []
        reactionSummaries = [:]
        clearTypingState()
        connectionBanner = nil
    }

    // ---- 设备面推送注册(P3) ---------------------------------------------------

    /// 登录成功后:请求通知授权 + 发起 APNs token 注册(token 走 AppDelegate
    /// 桥进 AwaitedPushTokenSource),并连设备面完成 RegisterDevice——
    /// 无 entitlement/授权被拒/系统回调失败一律空 token 降级注册(设备清单
    /// 仍登记,推送退化为服务端日志投递,对齐 android nopush 姿态)。
    private func startDeviceRegistration() {
        guard let host = loginHost, let service = service else { return }
        let userId = service.userId
        requestNotificationAuthorization()
        let tokenSource = AwaitedPushTokenSource()
        AppDelegate.tokenSink = tokenSource
        let plane = DevicePlaneService(
            userId: userId,
            deviceId: deviceId,
            deviceUrl: host.deviceUrl.absoluteString,
            osVersion: { ProcessInfo.processInfo.operatingSystemVersionString },
            deviceName: { UIDevice.current.name },
            transportFactory: { DarwinWsTransport(url: $0) },
            emit: { [weak self] event in
                Task { @MainActor in self?.handleDeviceEvent(event) }
            })
        devicePlane = plane
        plane.register(tokenSource: tokenSource).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self, self.devicePlane === plane else { return }
                // 终态正常路径经 Event 事件;这里只兜连接层失败。
                if case .failure(let err) = outcome {
                    self.toast = "设备面连接失败:\(err.localizedDescription)"
                }
            }
        }
    }

    private func handleDeviceEvent(_ event: DevicePlaneService.Event) {
        switch event {
        case .registered:
            toast = "设备已登记(推送目标:APNs token 或降级日志投递)"
        case .rejected(let code):
            toast = "设备注册被拒:\(codeName(code))"
        case .failed(let reason):
            toast = "设备注册失败:\(reason)"
        }
    }

    private func requestNotificationAuthorization() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) {
            _, _ in
            // 授权与否都发起远程通知注册:授权只影响展示,token 照取。
            DispatchQueue.main.async {
                UIApplication.shared.registerForRemoteNotifications()
            }
        }
    }

    // ---- 事件落位 --------------------------------------------------------------

    private func handle(_ event: ChatServiceEvent) {
        switch event {
        case .connectionStateChanged(let state):
            switch state {
            case .waitingReconnect:
                connectionBanner = "连接断开,重连中…"
            case .connected:
                if phase.isloggedIn { connectionBanner = nil }
            case .kicked:
                break
            default:
                break
            }
        case .loginSucceeded:
            phase = .loggedIn(userId: service?.userId ?? "")
            connectionBanner = nil
            refreshSessions()
            startDeviceRegistration()
        case .loginRejected(let code):
            toast = "登录被拒:\(codeName(code))"
            tearDownService()
        case .kicked(let reason):
            toast = "已在别处登录,被踢下线(\(reason))"
            tearDownService()
        case .messageReceived(let message):
            refreshSessions()
            if message.senderID == activePeer {
                chatMessages.append(message)
                service?.markRead(peerId: activePeer ?? "")
                refreshSessions()
            }
        case .offlineQueued(let content):
            toast = "离线入队,重连后自动重放:\(content)"
        case .offlineReplayed(let count):
            toast = "离线重放 \(count) 条"
            refreshSessions()
        case .reactionChanged(let messageId):
            reactionSummaries[messageId] = service?.reactions.tallies(messageId: messageId) ?? []
        case .typing:
            // 到点清扫兜底(服务端 stop 通知丢失时 TTL 过期仍能灭灯)。
            scheduleTypingSweep()
            refreshTyping()
        }
    }

    // ---- 会话/聊天面 ------------------------------------------------------------

    func openChat(peerId: String) {
        activePeer = peerId
        // store 面(本会话内存)最新在前 → 渲染旧→新;服务端历史异步合并。
        chatMessages = (service?.history(peerId: peerId) ?? []).reversed()
        service?.markRead(peerId: peerId)
        refreshSessions()
        rebuildReactionSummaries()
        refreshTyping()
        loadServerHistory(peerId: peerId)
    }

    func closeChat() {
        activePeer = nil
        chatMessages = []
        reactionSummaries = [:]
        clearTypingState()
    }

    /// 服务端 DM 历史(跨设备/重装真相面)按 messageID 去重合并——服务端
    /// 版本优先(带 id 的真相),本地无 id 的发送侧存档副本保留。
    private func loadServerHistory(peerId: String) {
        guard let service = service else { return }
        service.loadServerHistory(peerId: peerId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self, self.activePeer == peerId,
                    case .success(let resp) = outcome, resp.code == .ok
                else { return }
                self.chatMessages = Self.mergedMessages(
                    local: self.chatMessages, server: resp.messages)
                self.rebuildReactionSummaries()
            }
        }
    }

    /// 去重合并:同 id 保服务端版本,本地空 id 存档副本保留,时间戳升序。
    static func mergedMessages(
        local: [Chirp_Chat_ChatMessage], server: [Chirp_Chat_ChatMessage]
    ) -> [Chirp_Chat_ChatMessage] {
        var seen = Set<String>()
        var out: [Chirp_Chat_ChatMessage] = []
        for message in server where !message.messageID.isEmpty {
            if seen.insert(message.messageID).inserted { out.append(message) }
        }
        for message in local {
            if message.messageID.isEmpty || seen.insert(message.messageID).inserted {
                out.append(message)
            }
        }
        return out.sorted { $0.timestamp < $1.timestamp }
    }

    private func rebuildReactionSummaries() {
        guard let service = service else {
            reactionSummaries = [:]
            return
        }
        var next: [String: [ReactionIndex.Tally]] = [:]
        for message in chatMessages where !message.messageID.isEmpty {
            let tallies = service.reactions.tallies(messageId: message.messageID)
            if !tallies.isEmpty { next[message.messageID] = tallies }
        }
        reactionSummaries = next
    }

    func sendTapped(content: String) {
        guard let peerId = activePeer, let service = service,
            phase.isloggedIn, !content.isEmpty
        else { return }
        service.send(peerId: peerId, content: content).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(.sent):
                    // 发送即停 typing(web send 同款:清 timer + 补一条 stop)。
                    self.stopTypingIndicator()
                    // 存档副本已在管线内(save 先于响应),补一条本地渲染即可。
                    var message = Chirp_Chat_ChatMessage()
                    message.senderID = service.userId
                    message.receiverID = peerId
                    message.channelID = SessionIndex.dmChannelId(service.userId, peerId)
                    message.content = Data(content.utf8)
                    message.timestamp = Int64(Date().timeIntervalSince1970 * 1000)
                    if self.activePeer == peerId { self.chatMessages.append(message) }
                    self.refreshSessions()
                case .success(.queuedOffline):
                    break // 离线入队已有 toast 事件
                case .success(.blocked(let reason)):
                    self.toast = "已拦截:\(reason)"
                case .success(.rejected(let code)):
                    self.toast = "服务端拒绝:\(self.codeName(code))"
                case .success(.failed(let reason)):
                    self.toast = "发送失败:\(reason)"
                case .failure(let err):
                    self.toast = "发送失败:\(err.localizedDescription)"
                }
            }
        }
    }

    private func refreshSessions() {
        sessionSummaries = service?.sessions.summaries() ?? []
    }

    // ---- typing 上报/展示(P4a) ------------------------------------------------

    /// 输入框内容变化(ChatView onChange 驱动;空串不触发——web 语义)。
    func draftChanged(_ text: String) {
        guard !text.isEmpty, let peerId = activePeer, let service = service,
            phase.isloggedIn
        else { return }
        let channelId = SessionIndex.dmChannelId(service.userId, peerId)
        let at = Date()
        if let lastStart = typingLastStartAt,
            at.timeIntervalSince(lastStart) < Self.typingStartInterval
        {
            // 3s 内不重复上报(服务端本也会对重复 start 降温)。
        } else {
            typingLastStartAt = at
            service.sendTyping(channelId: channelId, isTyping: true)
        }
        // 最后键击 5s 后自动 stop。
        typingStopTask?.cancel()
        typingStopTask = Task { [weak self, channelId] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            guard !Task.isCancelled else { return }
            await MainActor.run {
                self?.typingLastStartAt = nil
                self?.service?.sendTyping(channelId: channelId, isTyping: false)
            }
        }
    }

    /// 发送/登出路径的立即停报(清 timer + 补一条 stop)。
    private func stopTypingIndicator() {
        typingStopTask?.cancel()
        typingStopTask = nil
        typingLastStartAt = nil
        if let peerId = activePeer, let service = service, phase.isloggedIn {
            service.sendTyping(
                channelId: SessionIndex.dmChannelId(service.userId, peerId), isTyping: false)
        }
    }

    /// TTL 过期清扫:stop 通知丢失时 6s 后仍能灭灯(留 0.5s 余量保证过期
    /// 判定成立;每次 start/stop 事件重置,到点重查快照)。
    private func scheduleTypingSweep() {
        typingSweepTask?.cancel()
        typingSweepTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 6_500_000_000)
            guard !Task.isCancelled else { return }
            await MainActor.run { self?.refreshTyping() }
        }
    }

    private func refreshTyping() {
        guard let peerId = activePeer, let service = service else {
            typingPeers = []
            return
        }
        let channelId = SessionIndex.dmChannelId(service.userId, peerId)
        let nowMs = Int64(Date().timeIntervalSince1970 * 1000)
        typingPeers = service.typists.typists(
            channelType: .private, channelId: channelId, nowMs: nowMs)
    }

    private func clearTypingState() {
        typingStopTask?.cancel()
        typingStopTask = nil
        typingSweepTask?.cancel()
        typingSweepTask = nil
        typingLastStartAt = nil
        typingPeers = []
    }

    /// 反应切换入口(mine → remove;否则 add)。成功路径的快照更新走
    /// .reactionChanged 事件;这里只兜连接层失败。
    func toggleReaction(messageId: String, emoji: String) {
        guard let service = service else { return }
        let mine = service.reactions.isMine(messageId: messageId, emoji: emoji)
        let promise = mine
            ? service.removeReaction(messageId: messageId, emoji: emoji)
            : service.addReaction(messageId: messageId, emoji: emoji)
        promise.onComplete { [weak self] outcome in
            Task { @MainActor in
                if case .failure(let err) = outcome {
                    self?.toast = "反应失败:\(err.localizedDescription)"
                }
            }
        }
    }

    private static let typingStartInterval: TimeInterval = 3

    private func codeName(_ code: Chirp_Common_ErrorCode) -> String {
        "\(code)"
    }

    /// 当前登录用户(气泡左右判定);未登录空串。
    var currentUserId: String {
        if case .loggedIn(let id) = phase { return id }
        return ""
    }

    private static let deviceIdKey = "chirp.device_id"
}
