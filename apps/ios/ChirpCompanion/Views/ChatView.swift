import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 聊天窗口(键寻址,DM 与群共用):该频道历史 + 实时收发。进页面即
/// markRead(索引+store)。P4a:长按气泡快捷反应、反应 chips、正在输入
/// 横幅、输入上报。P4e:顶部「加载更早的消息」翻页(beforeTimestamp
/// 游标)、群会话的发送者署名行与右上角群设置入口。
struct ChatView: View {
    @EnvironmentObject var model: AppModel
    let channelKey: String
    @State private var draft = ""
    @State private var showGroupSettings = false
    @FocusState private var inputFocused: Bool

    private var isGroup: Bool {
        model.activeChannel?.kind == .group && model.activeChannel?.key == channelKey
    }

    var body: some View {
        VStack(spacing: 0) {
            messageList
            typingBanner
            inputRow
        }
        .navigationTitle(model.chatTitle(forKey: channelKey))
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            if isGroup {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showGroupSettings = true } label: {
                        Image(systemName: "gearshape")
                    }
                    .accessibilityLabel("群设置")
                }
            }
        }
        .sheet(isPresented: $showGroupSettings) {
            if let groupId = model.activeChannel?.peerId {
                GroupSettingsView(groupId: groupId)
            }
        }
        .onAppear { model.openChat(key: channelKey) }
        .onDisappear { model.closeChat() }
    }

    private var messageList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 6) {
                    if model.chatHasMore {
                        Button {
                            model.loadEarlierTapped()
                        } label: {
                            if model.chatHistoryLoading {
                                ProgressView()
                            } else {
                                Text("加载更早的消息")
                                    .font(.subheadline)
                            }
                        }
                        .padding(.vertical, 6)
                        .disabled(model.chatHistoryLoading)
                    }
                    if model.chatMessages.isEmpty {
                        if model.chatHistoryLoading {
                            ProgressView().padding(.top, 40)
                        } else {
                            Text("暂无消息,发送第一条吧")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                                .padding(.top, 40)
                        }
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
            // 只追尾部:键在最后一条消息上——翻页向前补历史不动尾部(内容
            // 不变),新消息到达才滚到底(按 count 滚会在翻页时误跳底部)。
            .onChange(of: tailOffset) { _, newTail in
                guard let newTail else { return }
                withAnimation {
                    proxy.scrollTo(newTail, anchor: .bottom)
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

    private var tailOffset: String? {
        model.chatMessages.last.map(messageOffset)
    }

    private func bubble(_ message: Chirp_Chat_ChatMessage) -> some View {
        let mine = message.senderID == model.currentUserId
        return VStack(alignment: mine ? .trailing : .leading, spacing: 2) {
            // 群消息的非本人气泡带发送者署名行(web 同款;DM 里是显然的)。
            if isGroup && !mine && !message.senderID.isEmpty {
                Text(message.senderID)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            HStack {
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
            // 本地存档副本没有服务端 id,不可反应(服务端按 messageID 记账)。
            if !message.messageID.isEmpty,
                let tallies = model.reactionSummaries[message.messageID],
                !tallies.isEmpty
            {
                reactionChips(tallies, messageId: message.messageID, mine: mine)
            }
        }
        .contextMenu {
            if !message.messageID.isEmpty {
                ForEach(QuickReactions.all, id: \.self) { emoji in
                    Button {
                        model.toggleReaction(messageId: message.messageID, emoji: emoji)
                    } label: {
                        Text(emoji)
                    }
                }
            }
        }
    }

    /// 反应 chips:mine 高亮,点击切换(mine → remove;否则 add)。
    private func reactionChips(
        _ tallies: [ReactionIndex.Tally], messageId: String, mine: Bool
    ) -> some View {
        HStack(spacing: 4) {
            ForEach(tallies, id: \.emoji) { tally in
                Button {
                    model.toggleReaction(messageId: messageId, emoji: tally.emoji)
                } label: {
                    Text("\(tally.emoji) \(tally.count)")
                        .font(.caption2)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(
                            tally.isMine
                                ? Color.accentColor.opacity(0.35)
                                : Color(.secondarySystemBackground),
                            in: Capsule()
                        )
                }
                .buttonStyle(.plain)
            }
            if mine { Spacer(minLength: 48) } else { Spacer(minLength: 0) }
        }
    }

    /// 正在输入横幅(TTL 6s 过期自动灭灯;stop 通知即灭)。
    private var typingBanner: some View {
        Group {
            if let first = model.typingPeers.first {
                HStack(spacing: 4) {
                    Text("\(first) 正在输入…")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 2)
                .transition(.opacity)
            }
        }
    }

    private var inputRow: some View {
        HStack(spacing: 8) {
            TextField("消息", text: $draft, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .lineLimit(1...4)
                .focused($inputFocused)
                .onSubmit(send)
                .onChange(of: draft) { _, newText in
                    model.draftChanged(newText)
                }
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
        ChatView(channelKey: "p:alice|bob")
    }
    .environmentObject(AppModel())
}
