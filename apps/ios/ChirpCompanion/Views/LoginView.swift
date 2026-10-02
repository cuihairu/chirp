import ChirpAppCore
import SwiftUI

/// Phase-one login screen (aligned with the web LoginPage and the Android
/// shell): the user id IS the token in dev scaffold mode; the host field
/// carries the simulator default and stays editable for real devices.
struct LoginView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        Form {
            Section {
                TextField("用户 ID", text: $model.draft.userId)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
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
                    model.loginTapped()
                }
                .disabled(!model.draft.isValid)
            }

            if let validated = model.validatedDraft {
                Section {
                    Label(
                        "输入已校验(user \(validated.normalizedUserId))——连接与登录闭环在下一批落地,本批为工程骨架",
                        systemImage: "info.circle"
                    )
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                }
            }
        }
        .navigationTitle("Chirp")
    }
}

#Preview {
    LoginView()
        .environmentObject(AppModel())
}
