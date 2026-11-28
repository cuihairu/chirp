import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 聊天窗口(键寻址,DM 与群共用):该频道历史 + 实时收发。进页面即
/// markRead(索引+store)。P4a:长按气泡快捷反应、反应 chips、正在输入
/// 横幅、输入上报。P4e:顶部「加载更早的消息」翻页(beforeTimestamp
/// 游标)、群会话的发送者署名行与右上角群设置入口。消息引用(消息引用
/// P1,web/desktop 同款):长按「回复」挂引用,气泡上方渲染被引摘要
/// (仅本地已加载消息可解;已撤回/不在列表即「原消息已删除或不可见」)。
struct ChatView: View {
    @EnvironmentObject var model: AppModel
    let channelKey: String
    @State private var draft = ""
    @State private var replying: Chirp_Chat_ChatMessage?
    @State private var showGroupSettings = false
    @FocusState private var inputFocused: Bool

    private var isGroup: Bool {
        model.activeChannel?.kind == .group && model.activeChannel?.key == channelKey
    }

    var body: some View {
        VStack(spacing: 0) {
            messageList
            typingBanner
            replyPreview
            inputRow
        }
        .background(Theme.bg)
        .navigationTitle(model.chatTitle(forKey: channelKey))
        .navigationBarTitleDisplayMode(.inline)
        .toolbarBackground(Theme.bg, for: .navigationBar)
        .toolbarBackground(.visible, for: .navigationBar)
        .toolbarColorScheme(.dark, for: .navigationBar)
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
                                    .font(Theme.fontSecondary)
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
                                .font(Theme.fontSecondary)
                                .foregroundStyle(Theme.textTertiary)
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

    /// 署名(消息行/引用摘要/回复条共用):群聊=群昵称优先(aliases 快照,
    /// 2124 双路维护;空回退 ID,web sender-name 映射同款;修订号触发重渲,
    /// 变更即刻生效);DM=你/对方 id。
    private func displayName(_ uid: String) -> String {
        if isGroup {
            let alias = model.groupAlias(of: uid)
            return alias.isEmpty ? uid : alias
        }
        return uid == model.currentUserId ? "你" : uid
    }

    /// 被引消息摘要:仅本地已加载列表可解;已撤回视为不可见(web 同款)。
    private func quotedSummary(id: String) -> (senderName: String, content: String)? {
        guard let src = model.chatMessages.first(where: { $0.messageID == id }),
            !src.isRecalled
        else { return nil }
        return (
            displayName(src.senderID),
            String(data: src.content, encoding: .utf8) ?? ""
        )
    }

    /// 引用块:左竖线 + 「回复 · 署名」+ 两行钳制摘要;不可解即占位行。
    @ViewBuilder
    private func quotedBlock(_ message: Chirp_Chat_ChatMessage, mine: Bool) -> some View {
        if !message.replyToMessageID.isEmpty {
            if let quoted = quotedSummary(id: message.replyToMessageID) {
                HStack(alignment: .top, spacing: 5) {
                    Rectangle()
                        .fill(mine ? Theme.accentSoft : Theme.textTertiary)
                        .frame(width: 2.5)
                    VStack(alignment: .leading, spacing: 1) {
                        Text("回复 · \(quoted.senderName)")
                            .font(Theme.fontMeta)
                            .fontWeight(.semibold)
                            .foregroundStyle(Theme.textSecondary)
                        Text(quoted.content)
                            .font(Theme.fontMeta)
                            .lineLimit(2)
                            .foregroundStyle(Theme.textSecondary)
                    }
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: 260)
                .padding(.horizontal, 8)
            } else {
                Text("原消息已删除或不可见")
                    .font(Theme.fontMeta)
                    .foregroundStyle(Theme.textTertiary)
                    .frame(maxWidth: 260, alignment: .leading)
                    .padding(.horizontal, 8)
            }
        }
    }

    /// 气泡外形(02 稿):18 圆角、自己/对方各自的底角收 6 做尾角。
    private func bubbleShape(mine: Bool) -> some Shape {
        UnevenRoundedRectangle(
            cornerRadii: RectangleCornerRadii(
                topLeading: 18,
                bottomLeading: mine ? 18 : 6,
                bottomTrailing: mine ? 6 : 18,
                topTrailing: 18
            ),
            style: .continuous
        )
    }

    private func bubble(_ message: Chirp_Chat_ChatMessage) -> some View {
        let mine = message.senderID == model.currentUserId
        return VStack(alignment: mine ? .trailing : .leading, spacing: 2) {
            // 群消息的非本人气泡带发送者署名行(web 同款;DM 里是显然的)。
            if isGroup && !mine && !message.senderID.isEmpty {
                Text(displayName(message.senderID))
                    .font(Theme.fontMeta)
                    .foregroundStyle(Theme.textTertiary)
            }
            quotedBlock(message, mine: mine)
            HStack {
                if mine { Spacer(minLength: 48) }
                Text(String(data: message.content, encoding: .utf8) ?? "")
                    .font(Theme.fontBody)
                    .foregroundStyle(mine ? Color.white : Theme.textPrimary)
                    .padding(.horizontal, 13)
                    .padding(.vertical, 9)
                    .background(
                        mine ? Theme.accentStrong : Theme.surface2,
                        in: bubbleShape(mine: mine)
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
                // 已撤回不可作为引用源(web 同款:删除态无操作行)。
                if !message.isRecalled {
                    Button {
                        replying = message
                    } label: {
                        Label("回复", systemImage: "arrowshape.turn.up.left")
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
                        .font(Theme.fontChip)
                        .foregroundStyle(tally.isMine ? Theme.accentSoft : Theme.textSecondary)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(tally.isMine ? Theme.accentDim : Theme.surface2, in: Capsule())
                        .overlay(Capsule().strokeBorder(Theme.accent.opacity(0.4), lineWidth: 1))
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
                        .font(Theme.fontCaption)
                        .foregroundStyle(Theme.textSecondary)
                    Spacer()
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 2)
                .transition(.opacity)
            }
        }
    }

    /// 待发送引用条:被引摘要(引用源已撤回/不可解时降级显示占位)+ 取消。
    @ViewBuilder
    private var replyPreview: some View {
        if let replying {
            let quoted = quotedSummary(id: replying.messageID)
            HStack(alignment: .top, spacing: 6) {
                Rectangle()
                    .fill(Theme.accent)
                    .frame(width: 2.5)
                VStack(alignment: .leading, spacing: 1) {
                    Text("回复 · \(quoted?.senderName ?? displayName(replying.senderID))")
                        .font(Theme.fontMeta)
                        .fontWeight(.semibold)
                        .foregroundStyle(Theme.textSecondary)
                    if let quoted {
                        Text(quoted.content)
                            .font(Theme.fontMeta)
                            .lineLimit(1)
                            .foregroundStyle(Theme.textSecondary)
                    } else {
                        Text("原消息已删除或不可见")
                            .font(Theme.fontMeta)
                            .foregroundStyle(Theme.textTertiary)
                    }
                }
                Spacer()
                Button {
                    self.replying = nil
                } label: {
                    Image(systemName: "xmark")
                        .font(Theme.fontMeta)
                        .foregroundStyle(Theme.textSecondary)
                }
                .accessibilityLabel("取消回复")
            }
            .padding(.horizontal, 12)
            .padding(.top, 8)
            // 引用条并入底部输入坞(02 稿 inputbar):surface 连片,顶线压条。
            .background(Theme.surface)
            .overlay(alignment: .top) {
                Rectangle().fill(Theme.borderSoft).frame(height: 1)
            }
        }
    }

    private var canSend: Bool {
        !draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// 输入坞(02 稿 inputbar):surface 连片 + border-soft 顶线,字段
    /// 40 高胶囊(surface-2/border),发送 40 圆钮 accent+白纸飞机。
    private var inputRow: some View {
        HStack(spacing: 8) {
            TextField("消息", text: $draft, axis: .vertical)
                .font(Theme.fontBody)
                .foregroundStyle(Theme.textPrimary)
                .lineLimit(1...4)
                .focused($inputFocused)
                .onSubmit(send)
                .onChange(of: draft) { _, newText in
                    model.draftChanged(newText)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 9)
                .background(Capsule().fill(Theme.surface2))
                .overlay(Capsule().strokeBorder(Theme.border, lineWidth: 1))
                .accessibilityLabel("消息输入框")
            Button(action: send) {
                Image(systemName: "paperplane.fill")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(.white)
                    .frame(width: 40, height: 40)
                    .background(Circle().fill(Theme.accent))
            }
            .disabled(!canSend)
            .opacity(canSend ? 1 : 0.4)
            .accessibilityLabel("发送")
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(Theme.surface)
        .overlay(alignment: .top) {
            // 引用条显示时顶线归它,这里不重复压线。
            if replying == nil {
                Rectangle().fill(Theme.borderSoft).frame(height: 1)
            }
        }
    }

    private func send() {
        let content = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !content.isEmpty else { return }
        let replyToMessageId = replying?.messageID ?? ""
        draft = ""
        replying = nil
        model.sendTapped(content: content, replyToMessageId: replyToMessageId)
        inputFocused = true
    }
}

#Preview {
    NavigationStack {
        ChatView(channelKey: "p:alice|bob")
    }
    .environmentObject(AppModel())
}
