import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 好友面板(P4c):上=我的状态 + 加好友,中=待处理申请与我发出的申请,
/// 下=好友名册(带在线状态小圆点,点行进聊天)。名册/在线状态任一路拉取
/// 失败只置降级文案,面板照开——在线状态缺失只影响小圆点。
struct FriendsView: View {
    @EnvironmentObject var model: AppModel
    @State private var newFriend = ""

    var body: some View {
        NavigationStack {
            List {
                myPresenceSection
                addFriendSection
                pendingInSection
                pendingOutSection
                friendsSection
            }
            .navigationTitle("好友")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("刷新") { model.loadFriends() }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button("完成") { dismiss() }
                }
            }
        }
        .onAppear { model.loadFriends() }
    }

    @Environment(\.dismiss) private var dismiss

    // ---- 我的状态 ------------------------------------------------------------

    private var myPresenceSection: some View {
        Section {
            Picker("状态", selection: presenceBinding) {
                ForEach(Self.selectablePresence, id: \.self) { status in
                    Text(label(status)).tag(status)
                }
            }
            .pickerStyle(.menu)
        } header: {
            Text("我的状态")
        } footer: {
            Text("切换后广播给好友;离线状态由服务端在连接断开时下发。")
        }
    }

    private var presenceBinding: Binding<Chirp_Social_PresenceStatus> {
        Binding(
            get: { model.myPresence },
            set: { model.setMyPresence($0) })
    }

    private static let selectablePresence: [Chirp_Social_PresenceStatus] = [
        .online, .away, .dnd, .inGame, .inBattle, .offline,
    ]

    private func label(_ status: Chirp_Social_PresenceStatus) -> String {
        switch status {
        case .online: return "在线"
        case .away: return "离开"
        case .dnd: return "勿扰"
        case .inGame: return "游戏中"
        case .inBattle: return "战斗中"
        case .offline: return "离线(隐身)"
        default: return "未知"
        }
    }

    // ---- 加好友 --------------------------------------------------------------

    private var addFriendSection: some View {
        Section {
            HStack {
                TextField("对方用户 ID", text: $newFriend)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                Button("申请") {
                    model.addFriendTapped(targetUserId: newFriend)
                    newFriend = ""
                }
                .disabled(newFriend.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        } header: {
            Text("加好友")
        } footer: {
            Text("申请需对方同意;对方接受后会收到通知,名册随即出现。")
        }
    }

    // ---- 申请 ----------------------------------------------------------------

    private var pendingInSection: some View {
        Section {
            if model.pendingRequests.isEmpty {
                Text("没有待处理的申请")
                    .foregroundStyle(.secondary)
            } else {
                ForEach(model.pendingRequests, id: \.requestId) { request in
                    HStack {
                        Text(request.fromUserId).font(.headline)
                        Spacer()
                        Button("拒绝") {
                            model.respondToRequestTapped(
                                requestId: request.requestId, accept: false)
                        }
                        .font(.subheadline)
                        .buttonStyle(.borderless)
                        Button("同意") {
                            model.respondToRequestTapped(
                                requestId: request.requestId, accept: true)
                        }
                        .font(.subheadline)
                        .buttonStyle(.borderless)
                    }
                }
            }
        } header: {
            Text("待处理申请")
        }
    }

    private var pendingOutSection: some View {
        Section {
            if model.pendingOutIds.isEmpty {
                Text("没有已发出的申请")
                    .foregroundStyle(.secondary)
            } else {
                ForEach(model.pendingOutIds, id: \.self) { userId in
                    HStack {
                        Text(userId)
                        Spacer()
                        Text("等待对方同意")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        } header: {
            Text("我发出的申请")
        } footer: {
            Text("仅本机记录:对方同意或拒绝后,再次刷新面板即可看到结果。")
        }
    }

    // ---- 好友名册 ------------------------------------------------------------

    private var friendsSection: some View {
        Section {
            if let notice = model.socialPanelNotice {
                Text(notice)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            if model.socialFriends.isEmpty {
                Text("还没有好友")
                    .foregroundStyle(.secondary)
            } else {
                ForEach(model.socialFriends) { friend in
                    Button {
                        model.requestChat(peerId: friend.userId)
                        dismiss()
                    } label: {
                        friendRow(friend)
                    }
                    .buttonStyle(.plain)
                    .swipeActions(edge: .trailing) {
                        Button("删除", role: .destructive) {
                            model.removeFriendTapped(userId: friend.userId)
                        }
                    }
                }
            }
        } header: {
            Text("好友(\(model.socialFriends.count))")
        } footer: {
            Text("点行进聊天,左滑删除好友。灰点=超过 70s 未收到状态更新。")
        }
    }

    private func friendRow(_ friend: SocialFriendRow) -> some View {
        HStack {
            Image(systemName: "circle.fill")
                .font(.caption)
                .foregroundStyle(color(friend.dotColor))
            VStack(alignment: .leading, spacing: 2) {
                Text(friend.userId).font(.headline)
                if !friend.statusMessage.isEmpty {
                    Text(friend.statusMessage)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            Spacer()
            Text(friend.statusText)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private func color(_ dot: PresenceDot) -> Color {
        switch dot {
        case .online: return .green
        case .away: return .orange
        case .busy: return .red
        case .inGame: return .blue
        case .offline: return .secondary
        }
    }
}

#Preview {
    FriendsView().environmentObject(AppModel())
}