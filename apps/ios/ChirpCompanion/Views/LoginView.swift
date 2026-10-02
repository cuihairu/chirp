import ChirpAppCore
import SwiftUI

/// 登录面(对齐 web LoginPage 与 Android dev 壳):dev 阶段用户名即 token;
/// host 承载模拟器默认并保持可编辑(真机联调配 LAN 地址)。
struct LoginView: View {
    @EnvironmentObject private var model: AppModel
    @FocusState private var userIdFocused: Bool

    var body: some View {
        Form {
            Section {
                TextField("用户 ID", text: $model.draft.userId)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .focused($userIdFocused)
                    .submitLabel(.go)
                    .onSubmit { model.loginTapped() }
                TextField("服务器(host[:port],模拟器默认 127.0.0.1:7001)", text: $model.draft.host)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
            } footer: {
                Text("设备 ID:\(model.deviceId)")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            Section {
                Button("登录") {
                    userIdFocused = false
                    model.loginTapped()
                }
                .disabled(!model.draft.isValid)
            } footer: {
                Text("dev 联调:先起 chirp chat 网关(默认 7001);模拟器直连 Mac 环回,真机填局域网地址。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Chirp")
    }
}

#Preview {
    LoginView()
        .environmentObject(AppModel())
}
