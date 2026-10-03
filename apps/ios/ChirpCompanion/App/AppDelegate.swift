import ChirpAppCore
import UIKit

/// 远程通知桥:系统注册回调(UIApplicationDelegate)与注册发起方(AppModel)
/// 之间的静态管道——SwiftUI 生命周期里 delegate 由 @UIApplicationDelegateAdaptor
/// 持有,回调线程任意;offer 线程安全,迟到(超时后)自动丢弃。
final class AppDelegate: NSObject, UIApplicationDelegate {
    /// 当前等待 APNs token 的源;nil=无人在等,系统回调丢弃。
    static var tokenSink: AwaitedPushTokenSource?

    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil
    ) -> Bool {
        true
    }

    func application(
        _ application: UIApplication,
        didRegisterForRemoteNotificationsWithDeviceToken deviceToken: Data
    ) {
        // APNs device token → 十六进制串(服务端/herald 侧的常规口径)。
        let token = deviceToken.map { String(format: "%02x", $0) }.joined()
        Self.tokenSink?.offer(token)
    }

    func application(
        _ application: UIApplication,
        didFailToRegisterForRemoteNotificationsWithError error: Error
    ) {
        // 模拟器无 aps 环境 entitlement、真机未配推送证书等:走空 token 降级。
        Self.tokenSink?.offer(nil)
    }
}
