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
    /// 多端在线镜像(P4b:登录清单 + notify,platform 一槽)。
    @Published private(set) var onlineDevices: [OnlineDeviceIndex.Entry] = []
    /// 设备面注册清单(P4b:GET_USER_DEVICES,设备面就绪后拉取)。
    @Published var registeredDevices: [Chirp_AppNotification_DeviceInfo] = []
    /// 设备清单降级文案(设备面未注册/拉取失败;nil = 无事)。
    @Published var devicesPanelNotice: String?
    /// 好友名册 + 在线状态(P4c:FriendIndex/PresenceIndex 派生视图行)。
    @Published var socialFriends: [SocialFriendRow] = []
    /// 待我处理的入向申请(FRIEND_REQUEST_NOTIFY 累积)。
    @Published var pendingRequests: [FriendIndex.PendingIn] = []
    /// 我已发出、等对方确认的申请(纯本地,刷新即丢——服务端无出向查询)。
    @Published var pendingOutIds: [String] = []
    /// 我自己的在线状态(P4c:面板里可改,登录默认在线)。
    @Published var myPresence: Chirp_Social_PresenceStatus = .online
    /// 社交面板降级文案(名册/在线状态拉取失败;nil = 无事)。
    @Published var socialPanelNotice: String?
    /// 组队快照(P4d;nil=未入队,事件驱动刷新)。
    @Published var partySnapshot: PartyIndex.Snapshot?
    /// 入向我收到的组队邀请(INVITE_NOTIFY 累积,按 inviteId 去重)。
    @Published var partyInvites: [PartyIndex.Invite] = []
    /// 语音房间镜像(P4d;nil=不在房)。
    @Published var voiceRoom: VoiceIndex.RoomSnapshot?
    /// 游戏在线状态(P4d;nil=面不可用,面板降级)。
    @Published var gamePresenceState: GamePresenceIndex.State?
    /// 组队/语音面降级文案(登录被拒/失败;nil = 无事)。
    @Published var planesNotice: String?
    /// 群名单镜像(P4e:GET_USER_GROUPS 整表 + 群 notify 重拉)。
    @Published var groupRoster: [GroupIndex.Entry] = []
    /// 当前群面板的成员清单(P4e:面板打开时拉,动作后重拉)。
    @Published var groupMembers: [Chirp_Chat_GroupMember] = []
    /// 群面板降级文案(成员拉取失败;nil = 无事)。
    @Published var groupPanelNotice: String?
    /// 群昵称快照修订号:.groupAliasChanged 落主线程后自增。修订号无读者,
    /// 是纯粹的重渲信号——@Published 变更触发 objectWillChange,订阅壳层
    /// 视图 body 重估后从 service.aliases 快照读到新署名。
    @Published private(set) var aliasRevision = 0
    /// 历史分页(P4e):服务端还有更早一页 / 翻页请求在途。
    @Published private(set) var chatHasMore = false
    @Published private(set) var chatHistoryLoading = false
    /// 当前打开的会话(键寻址;DM 与群共用,web activeChannelKey 同位)。
    private(set) var activeChannel: SessionChannel?
    /// 跳转请求(键寻址):好友面板/新会话入口/建群成功置位,SessionsView
    /// 消费后清空(导航栈归它自己持有,本层只发请求)。
    @Published var pendingChatKey: String?
    /// 弹栈请求(被踢/退群关掉打开中的聊天面;chat 视图在导航栈里,本层
    /// 关不掉栈,SessionsView 消费后弹空)。单调递增,onChange 驱动。
    @Published private(set) var chatDismissToken = 0

    /// typing 上报节流(web ChatWindow 同款:start ≥3s 一次、最后键击 5s
    /// 后自动 stop、发送即 stop、空串不触发)。MainActor 上串行,无锁。
    private var typingLastStartAt: Date?
    private var typingStopTask: Task<Void, Never>?
    private var typingSweepTask: Task<Void, Never>?

    /// 设备识别(首次生成即持久化);登录面展示用。
    let deviceId: String
    var service: ChatSessionService?
    /// 设备面(app_gateway 5201)注册服务与本次登录用的 host 快照。
    var devicePlane: DevicePlaneService?
    /// 组队/语音面(P4d:party 7501 / voice 9001,各自独立连接,失败静默
    /// 降级——socket 不可用时聊天照常)。
    var partyPlane: PartyPlaneService?
    var voicePlane: VoicePlaneService?
    var loginHost: HostConfig?

    /// 推送点击深链暂存(P5):未登录时点击先存这里,登录成功后应用为
    /// pendingChatKey;登出/被踢不吞它(tap 已被用户消费,下次登录深链
    /// 仍到位——登出只清 pendingChatKey)。
    private var pendingPushRoute: String?

    init() {
        // UserDefaults 闭包对接 DeviceIdentity(key 与 web/android 同位)。
        let identity = DeviceIdentity(
            load: { UserDefaults.standard.string(forKey: Self.deviceIdKey) },
            save: { UserDefaults.standard.set($0, forKey: Self.deviceIdKey) })
        deviceId = identity.ensureDeviceId()
        // 通知面落点:前台呈现裁决与点击深链经 AppDelegate 静态桥进来
        // (willPresent/didReceive 回调跳主线程后读这里)。
        AppDelegate.model = self
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
            sessionSnapshot: Self.sessionSnapshotIO(for: userId),
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
        partyPlane?.shutdown()
        partyPlane = nil
        voicePlane?.shutdown()
        voicePlane = nil
        AppDelegate.tokenSink = nil
        phase = .loggedOut
        activeChannel = nil
        pendingChatKey = nil
        chatMessages = []
        sessionSummaries = []
        reactionSummaries = [:]
        onlineDevices = []
        registeredDevices = []
        devicesPanelNotice = nil
        socialFriends = []
        pendingRequests = []
        pendingOutIds = []
        myPresence = .online
        socialPanelNotice = nil
        partySnapshot = nil
        partyInvites = []
        voiceRoom = nil
        gamePresenceState = nil
        planesNotice = nil
        groupRoster = []
        groupMembers = []
        groupPanelNotice = nil
        chatHasMore = false
        chatHistoryLoading = false
        clearTypingState()
        connectionBanner = nil
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
            // 推送点击深链暂存(P5,登录期间点击落在这里):置 pendingChatKey
            // ——SessionsView 挂载前后的 onChange/onAppear 任一路径消费。
            if let route = pendingPushRoute {
                pendingPushRoute = nil
                pendingChatKey = route
            }
            refreshSessions()
            refreshGroupsRoster()
            startDeviceRegistration()
            startAuxiliaryPlanes()
            // 登录即对外在线(好友才能收到 PRESENCE_NOTIFY);被拒静默——
            // 非好友身份下服务端不广播,不影响聊天。
            setMyPresence(.online)
            rebuildSocial()
        case .loginRejected(let code):
            toast = "登录被拒:\(codeName(code))"
            tearDownService()
        case .kicked(let reason):
            toast = "已在别处登录,被踢下线(\(reason))"
            tearDownService()
        case .messageReceived(let message):
            refreshSessions()
            // 按频道路由:群消息归 'g:' 会话,DM 按发信人折 'p:' 键——只有
            // 落在当前打开会话里的才追加渲染,其余只更新列表预览/未读。
            let channel: SessionChannel?
            switch message.channelType {
            case .guild:
                channel = message.channelID.isEmpty ? nil : .group(message.channelID)
            default:
                channel = message.senderID.isEmpty
                    ? nil : .dm(selfId: currentUserId, peerId: message.senderID)
            }
            if let channel, channel == activeChannel {
                chatMessages.append(message)
                service?.markRead(key: channel.key)
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
        case .devicesChanged:
            onlineDevices = service?.devices.entries() ?? []
        case .friendsChanged, .presenceChanged:
            rebuildSocial()
        case .groupsChanged:
            refreshSessions()
            refreshGroupsRoster()
        case .groupAliasChanged:
            // 快照折叠与成员行重拉已在 service 层完成(2124 双路);壳层别名
            // 渲染(署名行/引用摘要/成员行)读 service.aliases 快照,这里自增
            // 修订号借 objectWillChange 触发重渲,署名变更即刻生效。
            aliasRevision += 1
        case .sessionDropped(let key, let reason):
            // 退群/被踢:数据面已在 service 清掉,这里刷新镜像;若正开着
            // 该会话,弹栈回列表(被踢给一条提示,退群走动作回执)。
            refreshSessions()
            refreshGroupsRoster()
            if activeChannel?.key == key {
                chatDismissToken += 1
                closeChat()
                if reason == .kicked { toast = "你已被移出该群组" }
            }
        }
    }

    // ---- 会话/聊天面 ------------------------------------------------------------

    /// 请求跳转到某对端的 DM 聊天窗口(新会话/好友面板入口)。
    func requestChat(peerId: String) {
        let peer = peerId.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !peer.isEmpty, !currentUserId.isEmpty else { return }
        pendingChatKey = SessionChannel.dm(selfId: currentUserId, peerId: peer).key
    }

    /// 请求跳转到某个群的聊天窗口(建群成功入口)。键直接落 'g:' 形态。
    func requestGroupChat(groupId: String) {
        guard !groupId.isEmpty else { return }
        pendingChatKey = SessionChannel.group(groupId).key
    }

    /// 会话行删除(P6,列表行滑动删除):数据面摘行,快照同步落盘。只移列表
    /// 行不清历史(store/服务端都留着),新消息再冒头;群行不开放删除——
    /// 名单引导(ensureGroup)会在下次刷新重建,群的移除走退群/被踢。
    func deleteSessionTapped(key: String) {
        guard let service = service else { return }
        service.sessions.remove(key: key)
        refreshSessions()
    }

    /// 推送点击深链(P5,AppDelegate.didReceive 经主线程跳入):已登录直接置
    /// pendingChatKey(SessionsView 消费导航);未登录暂存,登录成功后应用。
    /// 键形态已由 PushRouteParser 过结构门,这里只分登录态。
    func handlePushTap(channelKey: String) {
        if phase.isloggedIn {
            pendingChatKey = channelKey
        } else {
            pendingPushRoute = channelKey
        }
    }

    /// 打开会话(键寻址;DM 与群共用)。坏键(登出竞态)直接忽略。
    func openChat(key: String) {
        guard let channel = SessionChannel(key: key, selfId: currentUserId) else { return }
        activeChannel = channel
        chatHasMore = false
        chatHistoryLoading = false
        // store 面(本会话内存)最新在前 → 渲染旧→新;服务端历史异步合并。
        chatMessages = (service?.history(key: key) ?? []).reversed()
        service?.markRead(key: key)
        refreshSessions()
        rebuildReactionSummaries()
        refreshTyping()
        loadServerHistory(key: key)
    }

    func closeChat() {
        activeChannel = nil
        chatMessages = []
        reactionSummaries = [:]
        chatHasMore = false
        chatHistoryLoading = false
        clearTypingState()
    }

    /// 服务端频道历史(跨设备/重装真相面,DM/群共用)按 messageID 去重
    /// 合并——服务端版本优先(带 id 的真相),本地无 id 的发送侧存档副本
    /// 保留。首页拉取还带回 hasMore(更早页存在的真相面)。
    private func loadServerHistory(key: String, beforeTimestamp: Int64 = 0) {
        guard let service = service else { return }
        if beforeTimestamp == 0 { chatHistoryLoading = true }
        service.loadServerHistory(key: key, beforeTimestamp: beforeTimestamp)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self, self.activeChannel?.key == key,
                        case .success(let resp) = outcome, resp.code == .ok
                    else {
                        // 失败/已切走:复位在途旗(打开新会话会重置,这里兜
                        // 翻页失败后按钮卡死)。
                        self?.chatHistoryLoading = false
                        return
                    }
                    self.chatMessages = Self.mergedMessages(
                        local: self.chatMessages, server: resp.messages)
                    self.chatHasMore = resp.hasMore_p
                    self.chatHistoryLoading = false
                    self.rebuildReactionSummaries()
                }
            }
    }

    /// 「加载更早的消息」:以当前展示列表最旧一条的时间戳为游标翻页
    /// (web loadEarlier 同款)。在途防重入由按钮 disabled/loading 态承担。
    func loadEarlierTapped() {
        guard let key = activeChannel?.key, !chatHistoryLoading, let oldest = chatMessages.first
        else { return }
        chatHistoryLoading = true
        loadServerHistory(key: key, beforeTimestamp: oldest.timestamp)
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

    func sendTapped(content: String, replyToMessageId: String = "") {
        guard let channel = activeChannel, let service = service,
            phase.isloggedIn, !content.isEmpty
        else { return }
        service.send(
            channel: channel, content: content, replyToMessageId: replyToMessageId
        ).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(.sent):
                    // 发送即停 typing(web send 同款:清 timer + 补一条 stop)。
                    self.stopTypingIndicator()
                    // 存档副本已在管线内(save 先于响应),补一条本地渲染即可。
                    var message = Chirp_Chat_ChatMessage()
                    message.senderID = service.userId
                    message.channelType = channel.channelType
                    message.channelID = channel.channelId
                    if channel.kind == .dm { message.receiverID = channel.peerId }
                    message.content = Data(content.utf8)
                    message.timestamp = Int64(Date().timeIntervalSince1970 * 1000)
                    // 引用随本地副本渲染(服务端校验回填后同值)。
                    if !replyToMessageId.isEmpty { message.replyToMessageID = replyToMessageId }
                    if self.activeChannel == channel { self.chatMessages.append(message) }
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

    /// 会话列表镜像重建(拆分后供扩展文件复用)。
    func refreshSessions() {
        sessionSummaries = service?.sessions.summaries() ?? []
    }

    // ---- typing 上报/展示(P4a) ------------------------------------------------

    /// 输入框内容变化(ChatView onChange 驱动;空串不触发——web 语义)。
    /// 频道类型随当前会话:DM 报 PRIVATE、群报 GUILD(web 同款)。
    func draftChanged(_ text: String) {
        guard !text.isEmpty, let channel = activeChannel, let service = service,
            phase.isloggedIn
        else { return }
        let channelType = channel.channelType
        let channelId = channel.channelId
        let at = Date()
        if let lastStart = typingLastStartAt,
            at.timeIntervalSince(lastStart) < Self.typingStartInterval
        {
            // 3s 内不重复上报(服务端本也会对重复 start 降温)。
        } else {
            typingLastStartAt = at
            service.sendTyping(channelType: channelType, channelId: channelId, isTyping: true)
        }
        // 最后键击 5s 后自动 stop。
        typingStopTask?.cancel()
        typingStopTask = Task { [weak self, channelType, channelId] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            guard !Task.isCancelled else { return }
            await MainActor.run {
                self?.typingLastStartAt = nil
                self?.service?.sendTyping(
                    channelType: channelType, channelId: channelId, isTyping: false)
            }
        }
    }

    /// 发送/登出路径的立即停报(清 timer + 补一条 stop)。
    private func stopTypingIndicator() {
        typingStopTask?.cancel()
        typingStopTask = nil
        typingLastStartAt = nil
        if let channel = activeChannel, let service = service, phase.isloggedIn {
            service.sendTyping(
                channelType: channel.channelType,
                channelId: channel.channelId,
                isTyping: false)
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
        guard let channel = activeChannel, let service = service else {
            typingPeers = []
            return
        }
        let nowMs = Int64(Date().timeIntervalSince1970 * 1000)
        typingPeers = service.typists.typists(
            channelType: channel.channelType, channelId: channel.channelId, nowMs: nowMs)
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

    func codeName(_ code: Chirp_Common_ErrorCode) -> String {
        "\(code)"
    }

    /// 当前登录用户(气泡左右判定);未登录空串。
    var currentUserId: String {
        if case .loggedIn(let id) = phase { return id }
        return ""
    }

    /// 会话列表快照的字节面(P6):UserDefaults 按用户分键(载荷小、写频
    /// 低),编码/校验在 SessionIndex 内——与 DeviceIdentity 同款闭包缝。
    private static func sessionSnapshotIO(for userId: String) -> SessionIndex.SnapshotIO {
        let key = "chirp.sessions.\(userId)"
        return SessionIndex.SnapshotIO(
            load: { UserDefaults.standard.data(forKey: key) },
            save: { UserDefaults.standard.set($0, forKey: key) })
    }

    private static let deviceIdKey = "chirp.device_id"
}

/// 好友行的视图模型(P4c):名册 id + 快照状态。快照缺失或 TTL 过期一律
/// `.offline`(PresenceIndex.isFresh 已判),壳层不再二次判龄。
struct SocialFriendRow: Identifiable, Equatable {
    let userId: String
    let status: Chirp_Social_PresenceStatus
    let statusMessage: String

    var id: String { userId }

    /// 小圆点颜色:只有真在线态亮绿,其余按语义色。
    var dotColor: PresenceDot {
        switch status {
        case .online: return .online
        case .away: return .away
        case .dnd: return .busy
        case .inGame, .inBattle: return .inGame
        case .offline: return .offline
        default: return .offline
        }
    }

    var statusText: String {
        switch status {
        case .online: return "在线"
        case .away: return "离开"
        case .dnd: return "勿扰"
        case .inGame: return "游戏中"
        case .inBattle: return "战斗中"
        case .offline: return "离线"
        default: return "未知"
        }
    }
}

enum PresenceDot {
    case online
    case away
    case busy
    case inGame
    case offline
}

