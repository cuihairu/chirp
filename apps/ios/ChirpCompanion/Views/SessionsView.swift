import ChirpAppCore
import SwiftUI

/// 会话列表:SessionIndex 派生(最新在前),点击进聊天;支持任意对端发起
/// 新会话(P2 的 DM 面与 web/android dev 壳同位)。导航走 path:栈由本视图
/// 持有,好友面板/新会话入口通过 model.pendingChatPeerId 请求跳转。
struct SessionsView: View {
    @EnvironmentObject var model: AppModel
    @State private var newPeer = ""
    @State private var showDevices = false
    @State private var showFriends = false
    @State private var path: [String] = []

    var body: some View {
        NavigationStack(path: $path) {
            List {
                if model.sessionSummaries.isEmpty {
                    ContentUnavailableView(
                        "暂无会话",
                        systemImage: "bubble.left.and.bubble.right",
                        description: Text("在下方输入对端用户 ID 发起聊天")
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
                }
                if !model.sessionSummaries.isEmpty {
                    Section("会话") {
                        ForEach(model.sessionSummaries, id: \.peerId) { session in
                            NavigationLink(value: session.peerId) {
                                sessionRow(session)
                            }
                        }
                        // 会话删除随持久化批次落地(P2 索引是内存态)。
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
            .navigationDestination(for: String.self) { peerId in
                ChatView(peerId: peerId)
            }
        }
        .onChange(of: model.pendingChatPeerId) { _, peer in
            guard let peer, !peer.isEmpty else { return }
            model.pendingChatPeerId = nil
            path = [peer]
        }
    }

    private func sessionRow(_ session: SessionIndex.Summary) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(session.peerId).font(.headline)
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