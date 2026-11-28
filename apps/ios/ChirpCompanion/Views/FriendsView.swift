import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 好友面板(03 稿 token 化,P4c):好友/游戏好友分段(分段控制,同
/// android 好友页);好友视图=我的状态 + 加好友 + 申请收发 + 名册(在线
/// 状态小圆点,点行进聊天);游戏好友视图=游戏在线状态开关与绑定清单
/// (协议面无好友来源字段,来源标签按 android 同口径不落)。名册/在线
/// 状态任一路拉取失败只置降级文案,面板照开。
struct FriendsView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var newFriend = ""
    @State private var gameSegment = false

    var body: some View {
        NavigationStack {
            List {
                segmentRow
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 8, trailing: 0))
                    .listRowSeparator(.hidden)

                if gameSegment {
                    gameSection
                } else {
                    myPresenceSection
                    addFriendSection
                    pendingInSection
                    pendingOutSection
                    friendsSection
                }
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("好友")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Theme.bg, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        model.loadFriends()
                        if gameSegment { model.loadGamePresence() }
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .accessibilityLabel("刷新")
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button("完成") { dismiss() }
                }
            }
        }
        .onAppear { model.loadFriends() }
    }

    // ---- 分段控制(03 稿:surface-2 胶囊容器,选中段 accent 白字) ---------------

    private var segmentRow: some View {
        HStack(spacing: 4) {
            segmentButton("好友", game: false)
            segmentButton("游戏好友", game: true)
        }
        .padding(3)
        .background(Capsule().fill(Theme.surface2))
    }

    private func segmentButton(_ title: String, game: Bool) -> some View {
        Button {
            guard gameSegment != game else { return }
            gameSegment = game
            if game { model.loadGamePresence() }
        } label: {
            Text(title)
                .font(Theme.fontChip)
                .foregroundStyle(gameSegment == game ? Color.white : Theme.textSecondary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 6)
                .background(
                    Capsule().fill(gameSegment == game ? Theme.accent : Color.clear))
        }
        .buttonStyle(.plain)
    }

    // ---- 我的状态 ------------------------------------------------------------

    private var myPresenceSection: some View {
        Section {
            Picker("状态", selection: presenceBinding) {
                ForEach(Self.selectablePresence, id: \.self) { status in
                    Text(label(status)).tag(status)
                }
            }
            .font(Theme.fontBody)
            .foregroundStyle(Theme.textPrimary)
            .pickerStyle(.menu)
            .listRowBackground(Theme.surface)
        } header: {
            sectionHeader("我的状态")
        } footer: {
            sectionFooter("切换后广播给好友;离线状态由服务端在连接断开时下发。")
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
            HStack(spacing: 8) {
                TextField("对方用户 ID", text: $newFriend)
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .chirpField()
                Button {
                    model.addFriendTapped(targetUserId: newFriend)
                    newFriend = ""
                } label: {
                    Text("申请")
                        .frame(width: 64)
                }
                .buttonStyle(ChirpButtonStyle(kind: .ghost))
                .disabled(newFriend.trimmingCharacters(in: .whitespaces).isEmpty)
            }
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        } header: {
            sectionHeader("加好友")
        } footer: {
            sectionFooter("申请需对方同意;对方接受后会收到通知,名册随即出现。")
        }
    }

    // ---- 申请 ----------------------------------------------------------------

    private var pendingInSection: some View {
        Section {
            if model.pendingRequests.isEmpty {
                Text("没有待处理的申请")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            } else {
                ForEach(model.pendingRequests, id: \.requestId) { request in
                    HStack(spacing: 12) {
                        AvatarView(label: request.fromUserId, size: Theme.avatarSm)
                        Text(request.fromUserId)
                            .font(Theme.fontBodySemibold)
                            .foregroundStyle(Theme.textPrimary)
                            .lineLimit(1)
                        Spacer(minLength: 8)
                        Button("拒绝") {
                            model.respondToRequestTapped(
                                requestId: request.requestId, accept: false)
                        }
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.danger)
                        .buttonStyle(.plain)
                        Button("同意") {
                            model.respondToRequestTapped(
                                requestId: request.requestId, accept: true)
                        }
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.accentSoft)
                        .buttonStyle(.plain)
                    }
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .listRowBackground(Theme.surface)
                    .listRowSeparator(.hidden)
                }
            }
        } header: {
            sectionHeader("待处理申请")
        }
    }

    private var pendingOutSection: some View {
        Section {
            if model.pendingOutIds.isEmpty {
                Text("没有已发出的申请")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            } else {
                ForEach(model.pendingOutIds, id: \.self) { userId in
                    HStack(spacing: 12) {
                        AvatarView(label: userId, size: Theme.avatarSm)
                        Text(userId)
                            .font(Theme.fontBody)
                            .foregroundStyle(Theme.textPrimary)
                            .lineLimit(1)
                        Spacer(minLength: 8)
                        ChipText(text: "待同意", tone: .neutral)
                    }
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .listRowBackground(Theme.surface)
                    .listRowSeparator(.hidden)
                }
            }
        } header: {
            sectionHeader("我发出的申请")
        } footer: {
            sectionFooter("仅本机记录:对方同意或拒绝后,再次刷新面板即可看到结果。")
        }
    }

    // ---- 好友名册 ------------------------------------------------------------

    private var friendsSection: some View {
        Section {
            if let notice = model.socialPanelNotice {
                Text(notice)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
            if model.socialFriends.isEmpty {
                Text("还没有好友")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            } else {
                ForEach(model.socialFriends) { friend in
                    Button {
                        model.requestChat(peerId: friend.userId)
                        dismiss()
                    } label: {
                        friendRow(friend)
                    }
                    .buttonStyle(.plain)
                    .listRowBackground(Theme.surface)
                    .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
                    .listRowSeparator(.hidden)
                    .swipeActions(edge: .trailing) {
                        Button(role: .destructive) {
                            model.removeFriendTapped(userId: friend.userId)
                        } label: {
                            Label("删除", systemImage: "trash")
                        }
                    }
                }
            }
        } header: {
            sectionHeader("好友(\(model.socialFriends.count))")
        } footer: {
            sectionFooter("点行进聊天,左滑删除好友。灰点=超过 70s 未收到状态更新。")
        }
    }

    private func friendRow(_ friend: SocialFriendRow) -> some View {
        HStack(spacing: 12) {
            AvatarView(label: friend.userId, size: Theme.avatarMd)
                .overlay(alignment: .bottomTrailing) {
                    Circle()
                        .fill(dotColor(friend.dotColor))
                        .frame(width: 12, height: 12)
                        .overlay(Circle().strokeBorder(Theme.surface, lineWidth: 2.5))
                }
            VStack(alignment: .leading, spacing: 2) {
                Text(friend.userId)
                    .font(Theme.fontBodySemibold)
                    .foregroundStyle(Theme.textPrimary)
                    .lineLimit(1)
                if !friend.statusMessage.isEmpty {
                    Text(friend.statusMessage)
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.textSecondary)
                        .lineLimit(1)
                }
            }
            Spacer(minLength: 8)
            Text(friend.statusText)
                .font(Theme.fontMeta)
                .foregroundStyle(Theme.textTertiary)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }

    /// 语义点色(仅在线亮绿,其余按语义):token 色板映射。
    private func dotColor(_ dot: PresenceDot) -> Color {
        switch dot {
        case .online: return Theme.online
        case .away: return Theme.warn
        case .busy: return Theme.danger
        case .inGame: return Theme.accentSoft
        case .offline: return Theme.textTertiary
        }
    }

    // ---- 游戏好友视图(协议面无来源字段,android 同口径) ------------------------

    private var gameSection: some View {
        Section {
            if let presence = model.gamePresenceState {
                Toggle("对外展示游戏在线状态", isOn: Binding(
                    get: { presence.enabled },
                    set: { model.setGamePresenceEnabledTapped($0) }))
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .tint(Theme.accent)
                    .listRowBackground(Theme.surface)
                if presence.enabled {
                    if presence.gameIds.isEmpty {
                        Text("游戏在线状态未绑定:在游戏内绑定后,好友能看到你的对局状态")
                            .font(Theme.fontSecondary)
                            .foregroundStyle(Theme.textSecondary)
                            .listRowBackground(Color.clear)
                            .listRowSeparator(.hidden)
                    } else {
                        Text("游戏在线状态已开启")
                            .font(Theme.fontSecondary)
                            .foregroundStyle(Theme.textSecondary)
                            .listRowBackground(Color.clear)
                            .listRowSeparator(.hidden)
                        ForEach(presence.gameIds, id: \.self) { gameId in
                            Text(gameId)
                                .font(Theme.fontSecondary)
                                .foregroundStyle(Theme.textSecondary)
                                .listRowBackground(Color.clear)
                                .listRowSeparator(.hidden)
                        }
                    }
                } else {
                    Text("游戏在线状态已关闭")
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.textSecondary)
                        .listRowBackground(Color.clear)
                        .listRowSeparator(.hidden)
                }
            } else {
                Text("游戏状态面不可用(设备面未就绪)")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
        } header: {
            sectionHeader("游戏好友")
        } footer: {
            sectionFooter("在线状态数据面为 chat 服务 game_presence;游戏绑定关系由游戏侧维护。")
        }
    }

    // ---- 共用件 ---------------------------------------------------------------

    private func sectionHeader(_ title: String) -> some View {
        SectionLabel(text: title)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 12, leading: 2, bottom: -2, trailing: 0))
    }

    private func sectionFooter(_ text: String) -> some View {
        Text(text)
            .font(Theme.fontCaption)
            .foregroundStyle(Theme.textTertiary)
            .listRowBackground(Color.clear)
    }
}

#Preview {
    FriendsView().environmentObject(AppModel())
}
