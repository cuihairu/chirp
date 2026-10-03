import ChirpProtos
import ChirpProtocol
import Foundation

/// APNs token 的等待器(蓝本 Android FirebaseTokenSource 的 10s 封顶语义):
/// 系统的 didRegisterForRemoteNotifications 回调可能早于或晚于注册发起,
/// offer() 先到就缓存;fetchToken() 时未到则挂起等,超时一律 nil 降级
/// (dart device_api 对齐:空 token 也注册,推送退化为服务端日志投递)。
/// 回调恰一帧,offer 迟到(超时后)丢弃。
public final class AwaitedPushTokenSource: PushTokenSource {
    private let lock = NSLock()
    private let timeoutMs: Int64
    private let scheduler: Scheduler
    private var offered: String?
    private var offeredFlag = false
    private var waiter: ((String?) -> Void)?
    private var done = false

    public init(timeoutMs: Int64 = 10_000, scheduler: Scheduler = DispatchScheduler()) {
        self.timeoutMs = timeoutMs
        self.scheduler = scheduler
    }

    /// 壳层在系统回调线程上调用(token=nil 表示注册失败)。
    public func offer(_ token: String?) {
        let deliver: ((String?) -> Void)?
        lock.lock()
        defer { lock.unlock() }
        if done {
            return  // 超时已降级,迟到的系统回调丢弃
        }
        if let waiter = self.waiter {
            deliver = waiter
            self.waiter = nil
            done = true
        } else {
            offered = token
            offeredFlag = true
            deliver = nil
        }
        deliver?(token)
    }

    public func fetchToken(onResult: @escaping (String?) -> Void) throws {
        lock.lock()
        if offeredFlag {
            let token = offered
            lock.unlock()
            onResult(token)
            return
        }
        waiter = onResult
        lock.unlock()
        scheduler.post(delayMs: timeoutMs) { [weak self] in
            guard let self = self else { return }
            let fire: ((String?) -> Void)?
            self.lock.lock()
            if !self.done {
                fire = self.waiter
                self.waiter = nil
                self.done = true
            } else {
                fire = nil
            }
            self.lock.unlock()
            fire?(nil)
        }
    }
}

/// 设备面(app_gateway WS 5201)注册服务:连接 → 设备面 LOGIN(sdk_gateway
/// 的 REGISTER_DEVICE 经 GetAuthenticatedSession 守卫,未登录即拒)→
/// DeviceRegistrar(platform="ios", token 进 apns_token 槽)。UI-free 进包,
/// Linux 上假传输可测。
public final class DevicePlaneService {
    public enum Event {
        case registered
        case rejected(Chirp_Common_ErrorCode)
        case failed(String)
    }

    public let userId: String
    private let deviceId: String
    private let conn: ChatConnection
    private let registrar: DeviceRegistrar
    private let emit: (Event) -> Void
    private var stateBox: ConnState = .idle
    private let lock = NSLock()

    public var connectionState: ConnState {
        lock.lock()
        defer { lock.unlock() }
        return stateBox
    }

    public init(
        userId: String,
        deviceId: String,
        deviceUrl: String,
        appVersion: String = "",
        osVersion: @escaping () -> String = { "" },
        deviceName: @escaping () -> String = { "" },
        transportFactory: @escaping (String) -> WsTransport,
        scheduler: Scheduler = DispatchScheduler(),
        random: RandomSource = SystemRandomSource(),
        emit: @escaping (Event) -> Void
    ) {
        self.userId = userId
        self.deviceId = deviceId
        self.emit = emit
        conn = ChatConnection(
            url: deviceUrl, transportFactory: transportFactory,
            random: random, scheduler: scheduler)
        registrar = DeviceRegistrar(
            conn: conn,
            deviceId: { deviceId },
            appVersion: appVersion,
            osVersion: osVersion,
            deviceName: deviceName,
            platform: "ios",
            tokenSlot: .apns)
        _ = conn.onStateChange { [weak self] state in
            guard let self = self else { return }
            self.lock.lock()
            self.stateBox = state
            self.lock.unlock()
        }
    }

    /// connect → 设备面登录(dev 阶段用户名即 token)→ 注册推送目标。
    /// tokenSource 为 nil 或报 nil:空 token 照注册(降级)。
    public func register(tokenSource: (any PushTokenSource)? = nil)
        -> Promise<Chirp_Common_ErrorCode>
    {
        conn.connect().flatMap { [weak self] in
            guard let self = self else {
                return Promise<Chirp_Common_ErrorCode>.failed(
                    RequestError(.closed, message: "service released"))
            }
            return self.deviceLogin().flatMap { code in
                guard code == .ok else {
                    self.emit(.rejected(code))
                    return Promise<Chirp_Common_ErrorCode>.completed(code)
                }
                return self.registrar.register(userId: self.userId, pushToken: tokenSource)
                    .map { [weak self] registerCode in
                        guard let self = self else { return registerCode }
                        if registerCode == .ok {
                            self.emit(.registered)
                        } else {
                            self.emit(.rejected(registerCode))
                        }
                        return registerCode
                    }
            }
        }
    }

    private func deviceLogin() -> Promise<Chirp_Common_ErrorCode> {
        var request = Chirp_Auth_LoginRequest()
        request.token = userId  // dev 阶段用户名即 token,与 chat 面同口径
        request.deviceID = deviceId
        request.platform = "ios"
        return conn.request(spec: MsgSpecs.login, body: request).map { $0.code }
    }

    /// 注册设备清单(GET_USER_DEVICES,P4b 设备面板)。服务端按会话钉
    /// user_id,请求里的 id 只是形式对齐。设备面断开时直接以 CLOSED 终态
    /// 失败——壳层据此降级,不重连(重连/注册是 AppModel 的事)。
    public func loadDevices() -> Promise<Chirp_AppNotification_GetUserDevicesResponse> {
        guard connectionState == .connected else {
            return Promise<Chirp_AppNotification_GetUserDevicesResponse>.failed(
                RequestError(.closed, message: "device plane not connected"))
        }
        var request = Chirp_AppNotification_GetUserDevicesRequest()
        request.userID = userId
        return conn.request(spec: MsgSpecs.getUserDevices, body: request)
    }

    /// 断开设备面;幂等。
    public func shutdown() {
        conn.disconnect()
    }
}
