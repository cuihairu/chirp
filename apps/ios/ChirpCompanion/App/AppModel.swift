import ChirpAppCore
import ChirpProtos
import ChirpProtocol
import SwiftUI

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
    private(set) var activePeer: String?

    /// 设备识别(首次生成即持久化);登录面展示用。
    let deviceId: String
    private var service: ChatSessionService?

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
        phase = .loggedOut
        activePeer = nil
        chatMessages = []
        sessionSummaries = []
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
            refreshSessions()
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
        }
    }

    // ---- 会话/聊天面 ------------------------------------------------------------

    func openChat(peerId: String) {
        activePeer = peerId
        // history 最新在前 → 渲染旧→新。
        chatMessages = (service?.history(peerId: peerId) ?? []).reversed()
        service?.markRead(peerId: peerId)
        refreshSessions()
    }

    func closeChat() {
        activePeer = nil
        chatMessages = []
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
