import ChirpAppCore
import ChirpProtocol
import ChirpProtos
import UIKit
import UserNotifications

/// 设备/组队/语音/游戏状态面(P3/P4d):推送注册、副平面起停与组队/语音/游戏
/// 状态动作。AppModel 的 extension 拆分(纯搬运);状态字段与登录/事件
/// 编排仍在 AppModel.swift。
@MainActor
extension AppModel {
    // ---- 设备面推送注册(P3) ---------------------------------------------------

    /// 登录成功后:请求通知授权 + 发起 APNs token 注册(token 走 AppDelegate
    /// 桥进 AwaitedPushTokenSource),并连设备面完成 RegisterDevice——
    /// 无 entitlement/授权被拒/系统回调失败一律空 token 降级注册(设备清单
    /// 仍登记,推送退化为服务端日志投递,对齐 android nopush 姿态)。
    func startDeviceRegistration() {
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
    func startAuxiliaryPlanes() {
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
}
