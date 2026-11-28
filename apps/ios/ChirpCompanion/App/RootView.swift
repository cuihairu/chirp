import SwiftUI

/// 根切换:登录面 ↔ 会话列表面;连接横幅叠在两态之上(04/05 稿 token
/// 化:横幅 surface-3/文字三档,toast 胶囊 surface+描边+主文本)。
struct RootView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        VStack(spacing: 0) {
            if let banner = model.connectionBanner {
                Text(banner)
                    .font(Theme.fontCaption)
                    .foregroundStyle(Theme.textSecondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 6)
                    .padding(.horizontal, 14)
                    .background(Theme.surface3)
                    .overlay(alignment: .bottom) {
                        Rectangle()
                            .fill(Theme.borderSoft)
                            .frame(height: 1)
                    }
            }
            if model.phase.isloggedIn {
                SessionsView()
            } else {
                LoginView()
            }
        }
        .background(Theme.bg)
        .overlay(alignment: .bottom) {
            if let toast = model.toast {
                Text(toast)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textPrimary)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 10)
                    .background(
                        Capsule()
                            .fill(Theme.surface)
                            .overlay(
                                Capsule().strokeBorder(Theme.border, lineWidth: 1)
                            )
                    )
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
