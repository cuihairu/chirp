import ChirpAppCore
import UIKit
import UserNotifications

/// 远程通知桥:系统注册回调(UIApplicationDelegate)与注册发起方(AppModel)
/// 之间的静态管道——SwiftUI 生命周期里 delegate 由 @UIApplicationDelegateAdaptor
/// 持有,回调线程任意;offer 线程安全,迟到(超时后)自动丢弃。
///
/// P5 追加 UNUserNotificationCenterDelegate:前台呈现裁决与点击深链同走
/// 这座桥。UNUserNotificationCenter 回调在后台线程,业务读写一律
/// `Task { @MainActor }` 跳主线程(AppModel 是 @MainActor)。
final class AppDelegate: NSObject, UIApplicationDelegate, UNUserNotificationCenterDelegate {
    /// 当前等待 APNs token 的源;nil=无人在等,系统回调丢弃。
    static var tokenSink: AwaitedPushTokenSource?
    /// 通知面业务落点(AppModel 自挂,weak 不延长寿命);nil=无处可投。
    static weak var model: AppModel?

    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil
    ) -> Bool {
        // 前台呈现与点击深链要走本 delegate:不设则前台一律弹横幅、
        // 点击无深链(willPresent/didReceive 不会有人调)。
        UNUserNotificationCenter.current().delegate = self
        return true
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

    // MARK: - UNUserNotificationCenterDelegate

    /// 前台到达:正在看该会话不弹横幅,否则照常呈现(web ChatPage 同款)。
    /// 判定在主线程读 activeChannel;completionHandler 允许异步调用,
    /// 跳主线程后回。路由不出的推送照常呈现(只不导航,宁多勿漏)。
    func userNotificationCenter(
        _ center: UNUserNotificationCenter,
        willPresent notification: UNNotification,
        withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void
    ) {
        let route = PushRouteParser.route(from: notification.request.content.userInfo)
        Task { @MainActor in
            let active = AppDelegate.model?.activeChannel?.key
            let present = PushForegroundPolicy.shouldPresent(
                activeChannelKey: active, routeChannelKey: route?.channelKey)
            completionHandler(present ? [.banner, .sound] : [])
        }
    }

    /// 点击:chirp_data 反解会话键交给 AppModel 深链(未登录先暂存,
    /// 登录成功后应用);路由不出的点击不导航,照常唤起 App。
    func userNotificationCenter(
        _ center: UNUserNotificationCenter,
        didReceive response: UNNotificationResponse,
        withCompletionHandler completionHandler: @escaping () -> Void
    ) {
        if let route = PushRouteParser.route(from: response.notification.request.content.userInfo) {
            let key = route.channelKey
            Task { @MainActor in
                AppDelegate.model?.handlePushTap(channelKey: key)
            }
        }
        completionHandler()
    }
}
