import ChirpAppCore
import SwiftUI

/// 登录面(06 稿 token 化):dev 阶段用户名即 token;host 承载模拟器默认
/// 并保持可编辑(真机联调配 LAN 地址)。品牌区为几何声波占位,非真实 logo
/// (logo 是用户资产,不自行替换)。
struct LoginView: View {
    @EnvironmentObject private var model: AppModel
    @FocusState private var userIdFocused: Bool

    var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            ScrollView {
                VStack(spacing: 0) {
                    brand
                    form
                }
                .padding(.horizontal, 24)
                .frame(maxWidth: .infinity)
                .frame(minHeight: 520)
            }
        }
        .navigationTitle("Chirp")
        .navigationBarTitleDisplayMode(.inline)
        .toolbarBackground(Theme.bg, for: .navigationBar)
        .toolbarBackground(.visible, for: .navigationBar)
    }

    /// 品牌区(06 稿):56 圆角声波占位 + display 标题 + 次行副标题。
    private var brand: some View {
        VStack(spacing: 12) {
            RoundedRectangle(cornerRadius: Theme.radiusCard, style: .continuous)
                .fill(
                    LinearGradient(colors: [Theme.accent, Theme.accentStrong], startPoint: .top, endPoint: .bottom)
                )
                .frame(width: 56, height: 56)
                .overlay(
                    ChirpWaveMark()
                        .frame(width: 30, height: 30)
                )
            Text("登录到 Chirp")
                .font(Theme.fontDisplay)
                .foregroundStyle(Theme.textPrimary)
            Text("游戏好友与聊天,一个账号通行")
                .font(Theme.fontSecondary)
                .foregroundStyle(Theme.textSecondary)
        }
        .padding(.top, 36)
        .padding(.bottom, 28)
    }

    private var form: some View {
        VStack(alignment: .leading, spacing: 16) {
            VStack(alignment: .leading, spacing: 6) {
                Text("服务器")
                    .font(Theme.fontLabel)
                    .tracking(0.6)
                    .foregroundStyle(Theme.textTertiary)
                TextField("host[:port],模拟器默认 127.0.0.1:7001", text: $model.draft.host)
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .chirpField()
                Text("聊天/好友/组队/语音/设备五个平面按基址拼 /ws/{chat,social,party,voice,device}")
                    .font(Theme.fontCaption)
                    .foregroundStyle(Theme.textTertiary)
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("用户 ID")
                    .font(Theme.fontLabel)
                    .tracking(0.6)
                    .foregroundStyle(Theme.textTertiary)
                TextField("用户 ID", text: $model.draft.userId)
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .focused($userIdFocused)
                    .submitLabel(.go)
                    .onSubmit { model.loginTapped() }
                    .chirpField()
            }

            Button {
                userIdFocused = false
                model.loginTapped()
            } label: {
                Text("登录")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .primary))
            .disabled(!model.draft.isValid)

            Text("开发模式:输入用户 ID 即可登录 · 先起 chirp chat 网关(默认 7001);模拟器直连 Mac 环回,真机填局域网地址")
                .font(Theme.fontCaption)
                .foregroundStyle(Theme.textTertiary)
                .frame(maxWidth: .infinity, alignment: .center)
                .multilineTextAlignment(.center)

            if let deviceIdFootnote = deviceIdFootnote {
                Text(deviceIdFootnote)
                    .font(Theme.fontCaption)
                    .foregroundStyle(Theme.textTertiary)
                    .frame(maxWidth: .infinity, alignment: .center)
            }
        }
    }

    private var deviceIdFootnote: String? {
        model.draft.userId.isEmpty ? nil : "设备 ID:\(model.deviceId)"
    }
}

/// 声波占位标识(几何四条竖线,与 android/ic_wave、harmony 入口同形)。
struct ChirpWaveMark: View {
    var body: some View {
        Canvas { context, size in
            let bars: [(x: CGFloat, y0: CGFloat, y1: CGFloat)] = [
                (0.18, 0.5, 0.5), (0.42, 0.22, 0.78), (0.62, 0.32, 0.68), (0.82, 0.42, 0.58),
            ]
            for bar in bars {
                var path = Path()
                path.move(to: CGPoint(x: size.width * bar.x, y: size.height * bar.y0))
                path.addLine(to: CGPoint(x: size.width * bar.x, y: size.height * bar.y1))
                context.stroke(
                    path,
                    with: .color(.white),
                    style: StrokeStyle(lineWidth: 1.8, lineCap: .round)
                )
            }
        }
    }
}

#Preview {
    NavigationStack {
        LoginView()
    }
    .environmentObject(AppModel())
}
