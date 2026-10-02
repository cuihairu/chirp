import SwiftUI

/// Conversation list skeleton (P1: static empty state; the live list derives
/// from the message store in the closed-loop batch).
struct SessionsView: View {
    var body: some View {
        ContentUnavailableView(
            "暂无会话",
            systemImage: "bubble.left.and.bubble.right",
            description: Text("会话列表随登录+收发闭环批次接入")
        )
    }
}

#Preview {
    SessionsView()
}
