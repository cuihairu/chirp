import SwiftUI
import UIKit

@main
struct ChirpCompanionApp: App {
    // 显式类型参数 + import UIKit:仅 import SwiftUI 时 where 约束里的
    // UIApplicationDelegate 在使用点不可见,推导退化成 DelegateType=NSObject。
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
        }
    }
}
