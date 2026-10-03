import ChirpProtos
import Foundation

/// Async push-token seam (port of the Kotlin PushTokenSource; the real APNs
/// source needs Apple credentials and lives on the Darwin side — the default
/// wiring uses a nil-reporting placeholder). Implementations MUST report a
/// result exactly once and never throw: any failure is a nil token, and the
/// registrar degrades to dart device_api's path — register with an empty
/// `fcm_token` so the device still lists, pushes degrade to the server's
/// logging transport.
///
/// The requirement is declared `throws` so the Kotlin "throwing source"
/// defense vectors stay expressible: the registrar contains a throw exactly
/// like its runCatching/catch block did.
public protocol PushTokenSource: AnyObject {
    /// Report the token (nil = unavailable). Call `onResult` exactly once.
    func fetchToken(onResult: @escaping (String?) -> Void) throws
}

/// Exactly-once guard for the token callback: a source that both delivered
/// and then threw, or delivered twice, must not produce two registrations
/// (or none).
private final class Once {
    private let lock = NSLock()
    private var done = false

    /// Runs the body on the first call only.
    func run(_ body: () -> Void) {
        lock.lock()
        if done {
            lock.unlock()
            return
        }
        done = true
        lock.unlock()
        body()
    }
}

/// Which RegisterDeviceRequest slot the fetched token lands in. Kotlin only
/// ever fills `fcm_token` (its sole backend), so this stays a Swift-side
/// addition: iOS writes the APNs token to `apns_token` — the server has
/// stored all three slots verbatim since the apns batch (notification
/// handlers copy them through; UpdateDeviceToken picks first non-empty in
/// fcm → apns → push_kit order).
public enum PushTokenSlot {
    case fcm
    case apns
    case pushKit
}

/// Registers this install as a push target (device plane, app_gateway WS
/// 5201) — port of dart `device_api.dart#registerSelf` via the Kotlin
/// DeviceRegistrar, minus the local device-store bookkeeping the shell
/// doesn't have yet. The server pins `user_id` on the session, so the wire
/// field is informational.
///
/// The push token arrives asynchronously, so `register` fetches it first and
/// sends one RegisterDeviceRequest; the future completes with the server's
/// ErrorCode or fails with the connection-layer RequestError.
/// Kotlin → Swift: `CompletableFuture<Common.ErrorCode>` → `Promise`; the
/// `fun interface` seam → `PushTokenSource` protocol (throws requirement).
public final class DeviceRegistrar {
    private let conn: ChatConnection
    private let deviceId: () -> String
    private let appVersion: String
    private let osVersion: () -> String
    private let deviceName: () -> String
    private let platform: String
    private let tokenSlot: PushTokenSlot

    public init(
        conn: ChatConnection,
        deviceId: @escaping () -> String,
        appVersion: String = "",
        osVersion: @escaping () -> String = { "" },
        deviceName: @escaping () -> String = { "" },
        /// Per-device identity (Kotlin reports "android"); no vector
        /// depends on the value beyond its own platform.
        platform: String = "ios",
        tokenSlot: PushTokenSlot = .fcm
    ) {
        self.conn = conn
        self.deviceId = deviceId
        self.appVersion = appVersion
        self.osVersion = osVersion
        self.deviceName = deviceName
        self.platform = platform
        self.tokenSlot = tokenSlot
    }

    public func register(
        userId: String,
        pushToken: (any PushTokenSource)? = nil
    ) -> Promise<Chirp_Common_ErrorCode> {
        guard let pushToken = pushToken else {
            return sendRegister(userId: userId, fetched: nil)
        }
        // Same shape as the Kotlin registrar: the result future exists up
        // front (the token source may report asynchronously) and settles
        // when the single registration round does.
        let out = Promise<Chirp_Common_ErrorCode>()
        func deliver(_ fetched: String?) {
            sendRegister(userId: userId, fetched: fetched).onComplete { outcome in
                switch outcome {
                case .success(let code): out.complete(code)
                case .failure(let error): out.completeError(error)
                }
            }
        }
        // Exactly-once guard: a source that both delivered and then threw,
        // or threw after a sync delivery, must not send twice (or never).
        let once = Once()
        do {
            try pushToken.fetchToken { token in
                once.run { deliver(token) }
            }
        } catch {
            once.run { deliver(nil) }
        }
        return out
    }

    private func sendRegister(
        userId: String,
        fetched: String?
    ) -> Promise<Chirp_Common_ErrorCode> {
        var request = Chirp_AppNotification_RegisterDeviceRequest()
        request.userID = userId
        request.deviceID = deviceId()
        request.platform = platform
        // dart 降级对齐：无 token 也注册（token 槽留空，服务端推送降级
        // 为日志投递），设备清单仍登记。槽位由 tokenSlot 决定（iOS=apns）。
        switch tokenSlot {
        case .fcm: request.fcmToken = fetched ?? ""
        case .apns: request.apnsToken = fetched ?? ""
        case .pushKit: request.pushKitToken = fetched ?? ""
        }
        request.appVersion = appVersion
        request.osVersion = osVersion()
        request.deviceName = deviceName()
        return conn.request(spec: MsgSpecs.registerDevice, body: request).map { $0.code }
    }
}
