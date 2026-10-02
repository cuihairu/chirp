import SwiftUI

/// Root switch. P1 mounts only the logged-out branch plus skeleton screens;
/// the logged-in branch (会话 → 聊天) arrives with the pipeline wiring batch.
struct RootView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        NavigationStack {
            LoginView()
        }
    }
}

#Preview {
    RootView()
        .environmentObject(AppModel())
}
