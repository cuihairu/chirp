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
    @Published private(set) var registeredDevices: [Chirp_AppNotification_DeviceInfo] = []
    /// 设备清单降级文案(设备面未注册/拉取失败;nil = 无事)。
    @Published private(set) var devicesPanelNotice: String?
    /// 好友名册 + 在线状态(P4c:FriendIndex/PresenceIndex 派生视图行)。
    @Published private(set) var socialFriends: [SocialFriendRow] = []
    /// 待我处理的入向申请(FRIEND_REQUEST_NOTIFY 累积)。
    @Published private(set) var pendingRequests: [FriendIndex.PendingIn] = []
    /// 我已发出、等对方确认的申请(纯本地,刷新即丢——服务端无出向查询)。
    @Published private(set) var pendingOutIds: [String] = []
    /// 我自己的在线状态(P4c:面板里可改,登录默认在线)。
    @Published private(set) var myPresence: Chirp_Social_PresenceStatus = .online
    /// 社交面板降级文案(名册/在线状态拉取失败;nil = 无事)。
    @Published private(set) var socialPanelNotice: String?
    /// 组队快照(P4d;nil=未入队,事件驱动刷新)。
    @Published private(set) var partySnapshot: PartyIndex.Snapshot?
    /// 入向我收到的组队邀请(INVITE_NOTIFY 累积,按 inviteId 去重)。
    @Published private(set) var partyInvites: [PartyIndex.Invite] = []
    /// 语音房间镜像(P4d;nil=不在房)。
    @Published private(set) var voiceRoom: VoiceIndex.RoomSnapshot?
    /// 游戏在线状态(P4d;nil=面不可用,面板降级)。
    @Published private(set) var gamePresenceState: GamePresenceIndex.State?
    /// 组队/语音面降级文案(登录被拒/失败;nil = 无事)。
    @Published private(set) var planesNotice: String?
    /// 群名单镜像(P4e:GET_USER_GROUPS 整表 + 群 notify 重拉)。
    @Published private(set) var groupRoster: [GroupIndex.Entry] = []
    /// 当前群面板的成员清单(P4e:面板打开时拉,动作后重拉)。
    @Published private(set) var groupMembers: [Chirp_Chat_GroupMember] = []
    /// 群面板降级文案(成员拉取失败;nil = 无事)。
    @Published private(set) var groupPanelNotice: String?
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
    private var service: ChatSessionService?
    /// 设备面(app_gateway 5201)注册服务与本次登录用的 host 快照。
    private var devicePlane: DevicePlaneService?
    /// 组队/语音面(P4d:party 7501 / voice 9001,各自独立连接,失败静默
    /// 降级——socket 不可用时聊天照常)。
    private var partyPlane: PartyPlaneService?
    private var voicePlane: VoicePlaneService?
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
            loadRegisteredDevices()
            // game_presence 与推送共用设备面:面就绪即首拉开关与清单。
            loadGamePresence()
        case .rejected(let code):
            toast = "设备注册被拒:\(codeName(code))"
        case .failed(let reason):
            toast = "设备注册失败:\(reason)"
        }
    }

    /// 设备面板的注册清单(P4b):设备面就绪后拉取;面板打开时可再拉刷新。
    /// 拉取失败置降级文案,面板照开(在线端数据走 chat 面不受影响)。
    func loadRegisteredDevices() {
        guard let plane = devicePlane else {
            devicesPanelNotice = "设备面未就绪(注册未完成或已断开)"
            return
        }
        plane.loadDevices().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let resp):
                    if resp.code == .ok {
                        self.registeredDevices = resp.devices
                        self.devicesPanelNotice =
                            resp.devices.isEmpty ? "尚无注册设备" : nil
                    } else {
                        self.devicesPanelNotice = "设备清单拉取被拒:\(self.codeName(resp.code))"
                    }
                case .failure(let err):
                    self.devicesPanelNotice = "设备清单拉取失败:\(err.localizedDescription)"
                }
            }
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

    // ---- 组队/语音/游戏状态面(P4d) -------------------------------------------

    /// 登录成功后并行起组队/语音两条副平面;game_presence 复用设备面(注册
    /// 完成事件里首拉)。任一面失败静默降级——面板照开,数据空、横幅提示。
    private func startAuxiliaryPlanes() {
        guard let host = loginHost, let service = service else { return }
        let userId = service.userId

        let party = PartyPlaneService(
            userId: userId,
            partyUrl: host.partyUrl.absoluteString,
            transportFactory: { DarwinWsTransport(url: $0) },
            emit: { [weak self] event in
                Task { @MainActor in self?.handlePartyEvent(event) }
            })
        partyPlane = party
        party.login().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self, self.partyPlane === party else { return }
                if case .failure(let err) = outcome {
                    self.planesNotice = "组队面连接失败:\(err.localizedDescription)"
                }
            }
        }

        let voice = VoicePlaneService(
            userId: userId,
            voiceUrl: host.voiceUrl.absoluteString,
            transportFactory: { DarwinWsTransport(url: $0) },
            emit: { [weak self] event in
                Task { @MainActor in self?.handleVoiceEvent(event) }
            })
        voicePlane = voice
        voice.login().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self, self.voicePlane === voice else { return }
                if case .failure(let err) = outcome {
                    self.planesNotice = "语音面连接失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 面板刷新按钮:双面重拉快照(组队整表/语音在房重认领);面已死则
    /// 重新起(登录时起过的面在 teardown 前不该死,兜底而已)。
    func refreshAuxiliaryPlanes() {
        if partyPlane == nil || voicePlane == nil {
            startAuxiliaryPlanes()
            return
        }
        partyPlane?.fetchMyParty().onComplete { [weak self] _ in
            Task { @MainActor in self?.rebuildPartyMirrors() }
        }
        voicePlane?.restoreRoom().onComplete { [weak self] _ in
            Task { @MainActor in self?.rebuildVoiceMirror() }
        }
    }

    private func handlePartyEvent(_ event: PartyPlaneService.Event) {
        switch event {
        case .loggedIn:
            rebuildPartyMirrors()
        case .rejected(let code):
            planesNotice = "组队面登录被拒:\(codeName(code))"
        case .failed(let reason):
            planesNotice = "组队面失败:\(reason)"
        case .changed:
            rebuildPartyMirrors()
        }
    }

    private func handleVoiceEvent(_ event: VoicePlaneService.Event) {
        switch event {
        case .loggedIn:
            rebuildVoiceMirror()
        case .rejected(let code):
            planesNotice = "语音面登录被拒:\(codeName(code))"
        case .failed(let reason):
            planesNotice = "语音面失败:\(reason)"
        case .changed:
            rebuildVoiceMirror()
        }
    }

    /// 索引 → @Published 视图面。
    private func rebuildPartyMirrors() {
        partySnapshot = partyPlane?.party.party()
        partyInvites = partyPlane?.party.invites() ?? []
    }

    private func rebuildVoiceMirror() {
        voiceRoom = voicePlane?.roomIndex.room()
    }

    // ---- 组队动作 ------------------------------------------------------------

    func createPartyTapped() {
        guard let plane = partyPlane else { return }
        plane.createParty().onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: "组队已创建") }
        }
    }

    func inviteToPartyTapped(targetUserId: String) {
        let target = targetUserId.trimmingCharacters(in: .whitespaces)
        guard !target.isEmpty, let plane = partyPlane else { return }
        plane.invite(targetUserId: target).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: "已向 \(target) 发出组队邀请") }
        }
    }

    func respondToPartyInviteTapped(inviteId: String, accept: Bool) {
        guard let plane = partyPlane else { return }
        let promise = accept
            ? plane.acceptInvite(inviteId: inviteId)
            : plane.declineInvite(inviteId: inviteId)
        promise.onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: accept ? "已入队" : "已拒绝邀请") }
        }
    }

    func leavePartyTapped() {
        guard let plane = partyPlane else { return }
        plane.leaveParty().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(true):
                    self.toast = "已退队(队伍随之解散)"
                case .success(false):
                    self.toast = "已退队"
                case .failure(let err):
                    self.toast = "退队失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 就绪切换(成员面;队长动作走 kick/transfer,面板里按快照的队长位
    /// 显隐,这里只做 ready 与 leave 两个高频动作)。
    func togglePartyReadyTapped() {
        guard let plane = partyPlane else { return }
        let next = !(plane.party.selfMember(userId: currentUserId)?.ready ?? false)
        plane.setReady(next).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: next ? "已就绪" : "已取消就绪") }
        }
    }

    // ---- 语音房间动作 ---------------------------------------------------------

    func createVoiceRoomTapped(name: String) {
        guard let plane = voicePlane else { return }
        plane.createRoom(roomName: name).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: "语音房间已创建") }
        }
    }

    func joinVoiceRoomTapped(roomId: String) {
        let room = roomId.trimmingCharacters(in: .whitespaces)
        guard !room.isEmpty, let plane = voicePlane else { return }
        plane.joinRoom(roomId: room).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: "已进入语音房间") }
        }
    }

    func leaveVoiceRoomTapped() {
        guard let plane = voicePlane else { return }
        plane.leaveRoom().onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(true):
                    self.toast = "已离开语音房间"
                case .success(false):
                    self.toast = "不在语音房间内"
                case .failure(let err):
                    self.toast = "退房失败:\(err.localizedDescription)"
                }
            }
        }
    }

    func toggleVoiceMuteTapped() {
        guard let plane = voicePlane,
            let me = plane.roomIndex.room()?.participants.first(where: {
                $0.userId == plane.userId
            })
        else { return }
        let next = !(me.muted ?? false)
        plane.setMute(next).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: next ? "已闭麦" : "已开麦") }
        }
    }

    func toggleVoiceDeafenTapped() {
        guard let plane = voicePlane,
            let me = plane.roomIndex.room()?.participants.first(where: {
                $0.userId == plane.userId
            })
        else { return }
        let next = !(me.deafened ?? false)
        plane.setDeafen(next).onComplete { [weak self] outcome in
            Task { @MainActor in self?.reportPlaneCode(outcome, ok: next ? "已关听" : "已开听") }
        }
    }

    // ---- 游戏在线状态动作 -----------------------------------------------------

    /// 面板打开/下拉刷新时拉开关与游戏清单;失败降级为 nil。
    func loadGamePresence() {
        guard let plane = devicePlane else {
            gamePresenceState = nil
            return
        }
        plane.refreshGamePresence().onComplete { [weak self] _ in
            Task { @MainActor in
                guard let self = self, let plane = self.devicePlane else { return }
                self.gamePresenceState =
                    plane.gamePresence.unavailable() ? nil : plane.gamePresence.state()
            }
        }
    }

    func setGamePresenceEnabledTapped(_ enabled: Bool) {
        guard let plane = devicePlane else { return }
        plane.setGamePresenceEnabled(enabled).onComplete { [weak self] _ in
            Task { @MainActor in
                guard let self = self, let plane = self.devicePlane else { return }
                self.gamePresenceState =
                    plane.gamePresence.unavailable() ? nil : plane.gamePresence.state()
                if enabled && self.gamePresenceState?.enabled != true {
                    self.toast = "游戏状态开关未生效"
                }
            }
        }
    }

    /// 副平面请求的统一回执:ok 只在动作语义值得提示时展示。
    private func reportPlaneCode(
        _ outcome: Promise<Chirp_Common_ErrorCode>.Outcome, ok: String
    ) {
        switch outcome {
        case .success(let code) where code == .ok:
            toast = ok
        case .success(let code):
            toast = "操作被拒:\(codeName(code))"
        case .failure(let err):
            toast = "操作失败:\(err.localizedDescription)"
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

    func sendTapped(content: String) {
        guard let channel = activeChannel, let service = service,
            phase.isloggedIn, !content.isEmpty
        else { return }
        service.send(channel: channel, content: content).onComplete { [weak self] outcome in
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

    private func refreshSessions() {
        sessionSummaries = service?.sessions.summaries() ?? []
    }

    // ---- 好友/在线状态面(P4c) ---------------------------------------------

    /// 好友面板的整表刷新(打开面板时调):名册换入后按名册批量拉一次在线
    /// 状态。任一路失败置降级文案,面板照开——在线状态缺失只影响小圆点。
    func loadFriends() {
        guard let service = service else {
            socialPanelNotice = "尚未登录"
            return
        }
        socialPanelNotice = nil
        service.refreshFriends()
            .flatMap { [weak self] _ -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self, let service = self.service else {
                    return Promise<Chirp_Common_ErrorCode>.completed(.ok)
                }
                return service.pullPresence(of: service.friends.friends())
            }
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    self.rebuildSocial()
                    if case .failure(let err) = outcome {
                        self.socialPanelNotice = "名册拉取失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 改自己的在线状态(广播给好友)。失败只提示,面板状态保持原值。
    func setMyPresence(_ status: Chirp_Social_PresenceStatus) {
        guard let service = service else { return }
        service.setPresence(status).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.myPresence = status
                case .success(let code):
                    self.toast = "状态切换被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "状态切换失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 发起好友申请(空 id / 自己的 id 由 service 侧守卫,这里只做去空白)。
    func addFriendTapped(targetUserId: String) {
        let target = targetUserId.trimmingCharacters(in: .whitespaces)
        guard !target.isEmpty, let service = service else { return }
        service.addFriend(targetUserId: target).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.toast = "已向 \(target) 发出好友申请"
                    self.rebuildSocial()
                case .success(let code):
                    self.toast = "申请被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "申请失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 同意/拒绝一条入向申请;名册由 FRIEND_ACCEPTED_NOTIFY 补。
    func respondToRequestTapped(requestId: String, accept: Bool) {
        guard let service = service else { return }
        service.respondToRequest(requestId: requestId, accept: accept)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.rebuildSocial()
                    case .success(let code):
                        self.toast = "处理被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "处理失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 删好友(服务端对称幂等,对方走 notify)。
    func removeFriendTapped(userId: String) {
        guard let service = service else { return }
        service.removeFriend(targetUserId: userId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.rebuildSocial()
                case .success(let code):
                    self.toast = "删除被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "删除失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 名册/申请/在线状态 → @Published 视图行。TTL 过期(70s 无增量)的快照
    /// 按离线渲染,漏掉的 disconnect notify 由这一层兜底。
    private func rebuildSocial() {
        guard let service = service else {
            socialFriends = []
            pendingRequests = []
            pendingOutIds = []
            return
        }
        let nowMs = Int64(Date().timeIntervalSince1970 * 1000)
        socialFriends = service.friends.friends().map { userId in
            let fresh = service.presence.isFresh(userId, nowMs: nowMs)
            let entry = fresh ? service.presence.entry(userId) : nil
            return SocialFriendRow(
                userId: userId,
                status: entry?.status ?? .offline,
                statusMessage: entry?.statusMessage ?? "")
        }
        pendingRequests = service.friends.pendingIn()
        pendingOutIds = service.friends.pendingOut()
    }

    // ---- 群面板(P4e,web GroupDialogs 对齐)-----------------------------------

    /// 名单镜像 → @Published(sessions 列行标题也从这里取名)。
    private func refreshGroupsRoster() {
        groupRoster = service?.groups.entries() ?? []
    }

    /// 群设置面板打开:清旧清单再拉当前群成员(web 打开 Dialog 同款)。
    func openGroupPanel(groupId: String) {
        groupMembers = []
        groupPanelNotice = nil
        loadGroupMembers(groupId: groupId)
    }

    func loadGroupMembers(groupId: String) {
        guard let service = service else { return }
        service.loadGroupMembers(groupId: groupId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let resp):
                    if resp.code == .ok {
                        self.groupMembers = resp.members
                        self.groupPanelNotice = resp.members.isEmpty ? "尚无成员" : nil
                    } else {
                        self.groupPanelNotice = "成员拉取被拒:\(self.codeName(resp.code))"
                    }
                case .failure(let err):
                    self.groupPanelNotice = "成员拉取失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 建群;成功即跳进新群的聊天面(web 创建后进频道同款)。
    func createGroupTapped(name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, let service = service else { return }
        service.createGroup(name: trimmed).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let groupId) where !groupId.isEmpty:
                    self.toast = "群已创建"
                    self.refreshSessions()
                    self.refreshGroupsRoster()
                    self.requestGroupChat(groupId: groupId)
                case .success:
                    self.toast = "建群失败(服务端拒绝或断线)"
                case .failure(let err):
                    self.toast = "建群失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 邀人进群(INVITE 直接入群,无待确认态——web「添加成员」同款)。
    /// 成功后重拉成员清单(成员数与名单都是服务端权威)。
    func inviteToGroupTapped(groupId: String, targetUserId: String) {
        let target = targetUserId.trimmingCharacters(in: .whitespaces)
        guard !target.isEmpty, let service = service else { return }
        service.inviteToGroup(groupId: groupId, targetUserId: target)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.toast = "已添加 \(target)"
                        self.loadGroupMembers(groupId: groupId)
                    case .success(let code):
                        self.toast = "添加被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "添加失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 群主移出成员;成功后重拉成员清单。
    func kickGroupMemberTapped(groupId: String, targetUserId: String) {
        guard let service = service else { return }
        service.kickMember(groupId: groupId, targetUserId: targetUserId)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.toast = "已移出 \(targetUserId)"
                        self.loadGroupMembers(groupId: groupId)
                    case .success(let code):
                        self.toast = "移出被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "移出失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 退群;成功由 sessionDropped 事件弹栈关面板(web onLeft 退导航同款)。
    func leaveGroupTapped(groupId: String) {
        guard let service = service else { return }
        service.leaveGroup(groupId: groupId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.toast = "已退出群组"
                case .success(let code):
                    self.toast = "退群被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "退群失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 会话行/聊天面标题:DM 显示对端 id,群显示群名(名单缺名回落群 id,
    /// web 同款)。
    func sessionTitle(_ session: SessionIndex.Summary) -> String {
        switch session.kind {
        case .dm: return session.peerId
        case .group: return groupName(session.peerId)
        }
    }

    /// 聊天面标题(键寻址;ChatView navigationTitle 用)。
    func chatTitle(forKey key: String) -> String {
        guard let channel = SessionChannel(key: key, selfId: currentUserId) else {
            return key
        }
        switch channel.kind {
        case .dm: return channel.peerId
        case .group: return groupName(channel.peerId)
        }
    }

    /// 群名(缺名回落群 id);群主判定给设置面板用。
    func groupName(_ groupId: String) -> String {
        service?.groups.name(groupId: groupId) ?? groupId
    }

    var isGroupOwner: Bool {
        guard let channel = activeChannel, channel.kind == .group,
            let service = service,
            let entry = service.groups.entry(groupId: channel.peerId)
        else { return false }
        return entry.ownerId == currentUserId
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
