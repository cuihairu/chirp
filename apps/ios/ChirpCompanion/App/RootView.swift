import SwiftUI

/// 根切换:登录面 ↔ 会话列表面;连接横幅叠在两态之上。
struct RootView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        VStack(spacing: 0) {
            if let banner = model.connectionBanner {
                Text(banner)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 4)
                    .background(.quaternary)
            }
            if model.phase.isloggedIn {
                SessionsView()
            } else {
                LoginView()
            }
        }
        .overlay(alignment: .bottom) {
            if let toast = model.toast {
                Text(toast)
                    .font(.footnote)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(.ultraThinMaterial, in: Capsule())
                    .padding(.bottom, 12)
                    .task {
                        // 瞬态提示 2.5s 自清;新 toast 到来时 task 重建计时。
                        try? await Task.sleep(nanoseconds: 2_500_000_000)
                        if model.toast == toast { model.toast = nil }
                    }
            }
        }
    }
}

#Preview {
    RootView().environmentObject(AppModel())
}
