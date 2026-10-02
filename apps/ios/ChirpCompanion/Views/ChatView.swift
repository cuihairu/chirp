import ChirpProtos
import SwiftUI

/// 聊天窗口:该对端 DM 历史 + 实时收发。进页面即 markRead(索引+store)。
struct ChatView: View {
    @EnvironmentObject var model: AppModel
    let peerId: String
    @State private var draft = ""
    @FocusState private var inputFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            messageList
            inputRow
        }
        .navigationTitle(peerId)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { model.openChat(peerId: peerId) }
        .onDisappear { model.closeChat() }
    }

    private var messageList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 6) {
                    if model.chatMessages.isEmpty {
                        Text("暂无消息,发送第一条吧")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                            .padding(.top, 40)
                    }
                    ForEach(Array(model.chatMessages.enumerated()), id: \.offset) { _, message in
                        bubble(message)
                            .id(messageOffset(message))
                    }
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
            }
            .defaultScrollAnchor(.bottom)
            .onChange(of: model.chatMessages.count) { _, _ in
                if let last = model.chatMessages.last {
                    withAnimation {
                        proxy.scrollTo(messageOffset(last), anchor: .bottom)
                    }
                }
            }
        }
    }

    /// 稳定定位键:优先服务端 messageId,发送侧存档副本没有 id 就用内容哈希。
    private func messageOffset(_ message: Chirp_Chat_ChatMessage) -> String {
        message.messageID.isEmpty
            ? "local:\(message.senderID):\(message.content.hashValue)"
            : message.messageID
    }

    private func bubble(_ message: Chirp_Chat_ChatMessage) -> some View {
        let mine = message.senderID == model.currentUserId
        return HStack {
            if mine { Spacer(minLength: 48) }
            Text(String(data: message.content, encoding: .utf8) ?? "")
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(
                    mine ? Color.accentColor.opacity(0.28) : Color(.secondarySystemBackground),
                    in: RoundedRectangle(cornerRadius: 14)
                )
            if !mine { Spacer(minLength: 48) }
        }
    }

    private var inputRow: some View {
        HStack(spacing: 8) {
            TextField("消息", text: $draft, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .lineLimit(1...4)
                .focused($inputFocused)
                .onSubmit(send)
            Button("发送", action: send)
                .disabled(draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
        .padding(8)
    }

    private func send() {
        let content = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !content.isEmpty else { return }
        draft = ""
        model.sendTapped(content: content)
        inputFocused = true
    }
}

#Preview {
    NavigationStack {
        ChatView(peerId: "peer")
    }
    .environmentObject(AppModel())
}
