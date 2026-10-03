import ChirpAppCore
import SwiftUI

/// 会话列表:SessionIndex 派生(最新在前),点击进聊天(DM/群共用键寻址,
/// P4e);支持任意对端发起新 DM 与新建群组。导航走 path:栈由本视图持有,
/// 好友面板/新会话入口/建群成功通过 model.pendingChatKey 请求跳转,被踢/
/// 退群由 chatDismissToken 弹栈。
struct SessionsView: View {
    @EnvironmentObject var model: AppModel
    @State private var newPeer = ""
    @State private var newGroupName = ""
    @State private var showDevices = false
    @State private var showFriends = false
    @State private var showPartyVoice = false
    @State private var path: [String] = []

    var body: some View {
        NavigationStack(path: $path) {
            List {
                if model.sessionSummaries.isEmpty {
                    ContentUnavailableView(
                        "暂无会话",
                        systemImage: "bubble.left.and.bubble.right",
                        description: Text("在下方输入对端用户 ID 发起聊天,或新建一个群组")
                    )
                    .listRowBackground(Color.clear)
                }
                Section("新会话") {
                    HStack {
                        TextField("对端用户 ID", text: $newPeer)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                        Button("发起") {
                            let peer = newPeer.trimmingCharacters(in: .whitespaces)
                            guard !peer.isEmpty else { return }
                            newPeer = ""
                            model.requestChat(peerId: peer)
                        }
                        .disabled(newPeer.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                    HStack {
                        TextField("群组名称", text: $newGroupName)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                        Button("新建群组") {
                            let name = newGroupName
                            newGroupName = ""
                            model.createGroupTapped(name: name)
                        }
                        .disabled(newGroupName.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
                if !model.sessionSummaries.isEmpty {
                    Section("会话") {
                        ForEach(model.sessionSummaries, id: \.key) { session in
                            NavigationLink(value: session.key) {
                                sessionRow(session)
                            }
                        }
                        // 会话删除随持久化批次落地(索引是内存态)。
                    }
                }
            }
            .navigationTitle("chirp")
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

    private func sessionRow(_ session: SessionIndex.Summary) -> some View {
        HStack {
            Image(systemName: session.kind == .group ? "person.3" : "person")
                .foregroundStyle(.secondary)
                .font(.subheadline)
            VStack(alignment: .leading, spacing: 2) {
                Text(model.sessionTitle(session)).font(.headline)
                Text(session.lastMessage)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer()
            if session.unread > 0 {
                Text("\(session.unread)")
                    .font(.caption.bold())
                    .foregroundStyle(.white)
                    .padding(.horizontal, 7)
                    .padding(.vertical, 3)
                    .background(Circle().fill(.red))
            }
        }
    }
}

#Preview {
    SessionsView().environmentObject(AppModel())
}