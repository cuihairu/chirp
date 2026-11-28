import ChirpAppCore
import SwiftUI

/// 会话列表(01 稿 token 化):SessionIndex 派生(最新在前,快照跨启动存活
/// P6),点击进聊天(DM/群共用键寻址,P4e);DM 行滑动删除(List 结构保留,
/// 卡片化着色)。双主操作(新的私聊/新建群组)弹输入对话框。导航走 path:
/// 栈由本视图持有,好友面板/新会话入口/建群成功通过 model.pendingChatKey
/// 请求跳转,被踢/退群由 chatDismissToken 弹栈。
struct SessionsView: View {
    @EnvironmentObject var model: AppModel
    @State private var newPeer = ""
    @State private var newGroupName = ""
    @State private var showNewPeerDialog = false
    @State private var showNewGroupDialog = false
    @State private var showDevices = false
    @State private var showFriends = false
    @State private var showPartyVoice = false
    @State private var path: [String] = []

    var body: some View {
        NavigationStack(path: $path) {
            List {
                actions
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 8, trailing: 0))
                    .listRowSeparator(.hidden)

                if model.sessionSummaries.isEmpty {
                    emptyState
                        .listRowBackground(Color.clear)
                        .listRowSeparator(.hidden)
                }

                groupedSection(kind: .dm, title: "私聊")
                groupedSection(kind: .group, title: "群聊")
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("chirp")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Theme.bg, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showFriends = true } label: {
                        Image(systemName: "person.2")
                    }
                    .accessibilityLabel("好友面板")
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        showPartyVoice = true
                    } label: {
                        Image(systemName: "person.3")
                    }
                    .accessibilityLabel("组队与语音面板")
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        model.loadRegisteredDevices()
                        showDevices = true
                    } label: {
                        Image(systemName: "laptopcomputer.and.iphone")
                    }
                    .accessibilityLabel("设备面板")
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button("登出") { model.logoutTapped() }
                }
            }
            .sheet(isPresented: $showDevices) {
                DevicesView()
            }
            .sheet(isPresented: $showFriends) {
                FriendsView()
            }
            .sheet(isPresented: $showPartyVoice) {
                PartyVoiceView()
            }
            .navigationDestination(for: String.self) { key in
                ChatView(channelKey: key)
            }
            .alert("新的私聊", isPresented: $showNewPeerDialog) {
                TextField("对端用户 ID", text: $newPeer)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                Button("发起") {
                    let peer = newPeer.trimmingCharacters(in: .whitespaces)
                    newPeer = ""
                    guard !peer.isEmpty else { return }
                    model.requestChat(peerId: peer)
                }
                Button("取消", role: .cancel) { newPeer = "" }
            }
            .alert("新建群组", isPresented: $showNewGroupDialog) {
                TextField("群组名称", text: $newGroupName)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                Button("创建") {
                    let name = newGroupName
                    newGroupName = ""
                    guard !name.isEmpty else { return }
                    model.createGroupTapped(name: name)
                }
                Button("取消", role: .cancel) { newGroupName = "" }
            }
        }
        .onAppear {
            // 挂载前已置位的跳转请求(登录成功与挂载同帧 / 推送点击落在
            // 本视图尚未挂载的窗):onChange 只在挂载后变化时触发,这里
            // 补消费一次(重复消费读到 nil 自然无害)。
            if let key = model.pendingChatKey, !key.isEmpty {
                model.pendingChatKey = nil
                path = [key]
            }
        }
        .onChange(of: model.pendingChatKey) { _, key in
            guard let key, !key.isEmpty else { return }
            model.pendingChatKey = nil
            path = [key]
        }
        .onChange(of: model.chatDismissToken) { _, _ in
            // 被踢/退群:聊天面在栈顶,本层弹空回列表。
            path.removeAll()
        }
    }

    /// 双主操作(01 稿):primary 私聊 + ghost 建群。
    private var actions: some View {
        HStack(spacing: 10) {
            Button {
                showNewPeerDialog = true
            } label: {
                Label("新的私聊", systemImage: "bubble.left.and.bubble.right")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .primary))
            Button {
                showNewGroupDialog = true
            } label: {
                Label("新建群组", systemImage: "person.badge.plus")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .ghost))
        }
    }

    private var emptyState: some View {
        VStack(spacing: 6) {
            Text("还没有会话")
                .font(Theme.fontBodySemibold)
                .foregroundStyle(Theme.textSecondary)
            Text("发起新的私聊,或创建一个群组")
                .font(Theme.fontCaption)
                .foregroundStyle(Theme.textTertiary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 28)
    }

    /// 分组卡段(01 稿):组标签 + 卡片化的行(分割线做 1px border-soft)。
    private func groupedSection(kind: SessionChannel.Kind, title: String) -> some View {
        Section {
            let rows = model.sessionSummaries.filter { $0.kind == kind }
            ForEach(rows, id: \.key) { session in
                NavigationLink(value: session.key) {
                    sessionRow(session)
                }
                .buttonStyle(.plain)
                .listRowBackground(Theme.surface)
                .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
                .listRowSeparator(.hidden)
                .swipeActions(edge: .trailing) {
                    // 只对 DM 行开放:群行删了会被名单引导(ensureGroup)在
                    // 下次刷新重建,删除无效——群的移除走群面板的退群/被踢。
                    if session.kind == .dm {
                        Button(role: .destructive) {
                            model.deleteSessionTapped(key: session.key)
                        } label: {
                            Label("删除", systemImage: "trash")
                        }
                    }
                }
            }
            if rows.isEmpty {
                Text("暂无\(title)")
                    .font(Theme.fontCaption)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
        } header: {
            SectionLabel(text: title)
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets(top: 12, leading: 2, bottom: -2, trailing: 0))
        }
    }

    /// 行时间(01 稿):今天显示 HH:mm,更早显示 MM-dd(与通知中心同口径)。
    private func rowTime(_ stamp: Int64) -> String {
        let date = Date(timeIntervalSince1970: TimeInterval(stamp) / 1000)
        let fmt = DateFormatter()
        fmt.dateFormat = Calendar.current.isDateInToday(date) ? "HH:mm" : "MM-dd"
        return fmt.string(from: date)
    }

    private func sessionRow(_ session: SessionIndex.Summary) -> some View {
        HStack(spacing: 12) {
            AvatarView(label: model.sessionTitle(session), size: Theme.avatarMd)
            VStack(alignment: .leading, spacing: 2) {
                Text(model.sessionTitle(session))
                    .font(Theme.fontBodySemibold)
                    .foregroundStyle(Theme.textPrimary)
                    .lineLimit(1)
                Text(session.lastMessage)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 8)
            VStack(alignment: .trailing, spacing: 6) {
                Text(rowTime(session.lastTimestamp))
                    .font(Theme.fontMeta)
                    .foregroundStyle(Theme.textTertiary)
                if session.unread > 0 {
                    Text("\(session.unread)")
                        .font(Theme.fontMeta)
                        .foregroundStyle(.white)
                        .padding(.horizontal, 7)
                        .padding(.vertical, 3)
                        .background(Capsule().fill(Theme.accent))
                }
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }
}

#Preview {
    SessionsView().environmentObject(AppModel())
}
