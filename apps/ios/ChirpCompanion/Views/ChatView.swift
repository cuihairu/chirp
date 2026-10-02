import SwiftUI

/// Chat window skeleton (P1: disabled controls; wiring — message log, ack,
/// offline queue — lands with the closed-loop batch).
struct ChatView: View {
    var body: some View {
        VStack(spacing: 0) {
            ContentUnavailableView(
                "聊天窗口骨架",
                systemImage: "text.bubble",
                description: Text("消息收发随登录闭环批次接入")
            )
            HStack {
                TextField("消息", text: .constant(""))
                    .textFieldStyle(.roundedBorder)
                Button("发送") {}
                    .disabled(true)
            }
            .padding(8)
        }
        .navigationTitle("聊天")
        .navigationBarTitleDisplayMode(.inline)
    }
}

#Preview {
    NavigationStack {
        ChatView()
    }
}
