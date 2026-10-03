import SwiftUI

@main
struct ChirpCompanionApp: App {
    @StateObject private var model = AppModel()
    @UIApplicationDelegateAdaptor private var appDelegate = AppDelegate()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
        }
    }
}
