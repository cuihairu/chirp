import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 群设置面板(10 稿 token 化,P4e,web GroupSettingsDialog 对齐):添加
/// 成员(INVITE 直接入群)、成员清单(群主标识、群主可移出他人、
/// MODERATOR+ 长按禁言/改群昵称)、退出群组(确认后退,sessionDropped
/// 事件把聊天面弹回列表)。sheet 形态,完成即关。
struct GroupSettingsView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let groupId: String
    @State private var inviteTarget = ""
    @State private var confirmLeave = false
    @State private var aliasTarget: String?
    @State private var aliasInput = ""
    @State private var muteTarget: String?
    @State private var muteInput = ""

    private var ownerId: String? {
        model.groupRoster.first { $0.groupId == groupId }?.ownerId
    }

    private var amOwner: Bool {
        ownerId == model.currentUserId
    }

    /// MODERATOR+ 可禁言他人/改群昵称(角色以名册下发为准,web
    /// isModeratorPlus 同口径);名册未载时以群主身份兜底。
    private var canModerate: Bool {
        if let mine = model.groupMembers.first(where: { $0.userID == model.currentUserId }) {
            return mine.role.rawValue >= Chirp_Chat_GroupMemberRole.moderator.rawValue
        }
        return amOwner
    }

    /// 禁言徽章看服务端时间戳,本地时钟到点自动熄(不持久化本地状态)。
    private func isMuted(_ member: Chirp_Chat_GroupMember) -> Bool {
        member.mutedUntilTs > Int64(Date.now.timeIntervalSince1970 * 1000)
    }

    var body: some View {
        NavigationStack {
            List {
                inviteSection
                membersSection
                leaveSection
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("群组设置:\(model.groupName(groupId))")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Theme.bg, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        model.loadGroupMembers(groupId: groupId)
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .accessibilityLabel("刷新成员")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("完成") { dismiss() }
                }
            }
            .confirmationDialog("退出这个群组?", isPresented: $confirmLeave, titleVisibility: .visible) {
                Button("退出群组", role: .destructive) {
                    model.leaveGroupTapped(groupId: groupId)
                    dismiss()
                }
                Button("取消", role: .cancel) {}
            }
            .alert(
                "群昵称", isPresented: Binding(
                    get: { aliasTarget != nil },
                    set: { if !$0 { aliasTarget = nil } })
            ) {
                TextField("留空清除", text: $aliasInput)
                Button("确定") {
                    guard let target = aliasTarget else { return }
                    aliasTarget = nil
                    model.setGroupAliasTapped(
                        groupId: groupId, targetUserId: target,
                        alias: aliasInput.trimmingCharacters(in: .whitespaces))
                }
                Button("取消", role: .cancel) { aliasTarget = nil }
            } message: {
                Text("设置该成员在本群的显示名,留空即清除。")
            }
            .alert(
                "禁言(分钟)", isPresented: Binding(
                    get: { muteTarget != nil },
                    set: { if !$0 { muteTarget = nil } })
            ) {
                TextField("如 30(上限 43200)", text: $muteInput)
                    .keyboardType(.numberPad)
                Button("确定") {
                    guard let target = muteTarget else { return }
                    muteTarget = nil
                    // 0/非数字/超上限直接放弃(web/harmony 同款静默门)。
                    guard let minutes = Int(muteInput.trimmingCharacters(in: .whitespaces)),
                        minutes > 0, minutes <= 43200
                    else { return }
                    model.setGroupMuteTapped(
                        groupId: groupId, targetUserId: target,
                        seconds: Int64(minutes) * 60)
                }
                Button("取消", role: .cancel) { muteTarget = nil }
            } message: {
                Text("到期自动解禁;填 0 之外的超上限值将被拒绝。")
            }
            .onAppear { model.openGroupPanel(groupId: groupId) }
        }
    }

    // ---- 添加成员 -------------------------------------------------------------

    private var inviteSection: some View {
        Section {
            HStack(spacing: 8) {
                TextField("成员用户 ID", text: $inviteTarget)
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .chirpField()
                Button {
                    let target = inviteTarget
                    inviteTarget = ""
                    model.inviteToGroupTapped(
                        groupId: groupId, targetUserId: target)
                } label: {
                    Text("添加")
                        .frame(width: 64)
                }
                .buttonStyle(ChirpButtonStyle(kind: .ghost))
                .disabled(inviteTarget.trimmingCharacters(in: .whitespaces).isEmpty)
            }
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        } header: {
            sectionHeader("添加成员")
        }
    }

    // ---- 成员 ----------------------------------------------------------------

    private var membersSection: some View {
        Section {
            if let notice = model.groupPanelNotice {
                Text(notice)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
            ForEach(model.groupMembers, id: \.userID) { member in
                memberRow(member)
                    .listRowBackground(Theme.surface)
                    .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
                    .listRowSeparator(.hidden)
            }
        } header: {
            sectionHeader("成员")
        }
    }

    private func memberRow(_ member: Chirp_Chat_GroupMember) -> some View {
        HStack(spacing: 12) {
            AvatarView(label: displayName(member), size: Theme.avatarSm)
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    // 显示名:群昵称 > 用户名 > 用户 ID(web displayName 同口径)。
                    Text(displayName(member))
                        .font(Theme.fontBodySemibold)
                        .foregroundStyle(Theme.textPrimary)
                        .lineLimit(1)
                    if member.userID == ownerId {
                        ChipText(text: "群主", tone: .game)
                    }
                }
                if !member.alias.isEmpty {
                    // 设了群昵称时副行回显真实身份(web secondary 同款)。
                    Text(member.username.isEmpty ? member.userID : member.username)
                        .font(Theme.fontMeta)
                        .foregroundStyle(Theme.textTertiary)
                        .lineLimit(1)
                }
            }
            Spacer(minLength: 8)
            if isMuted(member) {
                // 徽章只反映服务端 muted_until_ts,不落本地状态。
                ChipText(text: "已禁言", tone: .warn)
            }
            // 只有群主能移出成员,且不指向自己(web 同款)。
            if amOwner && member.userID != model.currentUserId {
                Button {
                    model.kickGroupMemberTapped(
                        groupId: groupId, targetUserId: member.userID)
                } label: {
                    Text("移出")
                }
                .font(Theme.fontSecondary)
                .foregroundStyle(Theme.danger)
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
        // 长按菜单:改群昵称(本人或 MODERATOR+,web/harmony 同门);禁言
        // /解禁仅 MODERATOR+ 且不指向自己——解禁直接走 seconds=0。
        .contextMenu {
            if member.userID == model.currentUserId || canModerate {
                Button {
                    aliasInput = member.alias
                    aliasTarget = member.userID
                } label: {
                    Label("改群昵称", systemImage: "pencil")
                }
            }
            if canModerate && member.userID != model.currentUserId {
                if isMuted(member) {
                    Button {
                        model.setGroupMuteTapped(
                            groupId: groupId, targetUserId: member.userID, seconds: 0)
                    } label: {
                        Label("解除禁言", systemImage: "mic")
                    }
                } else {
                    Button {
                        muteInput = ""
                        muteTarget = member.userID
                    } label: {
                        Label("禁言…", systemImage: "mic.slash")
                    }
                }
            }
        }
    }

    /// 显示名口径与旧版一致:群昵称 > 用户名 > 用户 ID。
    private func displayName(_ member: Chirp_Chat_GroupMember) -> String {
        member.alias.isEmpty
            ? (member.username.isEmpty ? member.userID : member.username)
            : member.alias
    }

    // ---- 退出 ----------------------------------------------------------------

    private var leaveSection: some View {
        Section {
            Button {
                confirmLeave = true
            } label: {
                Text("退出群组")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .danger))
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        }
    }

    private func sectionHeader(_ title: String) -> some View {
        SectionLabel(text: title)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 12, leading: 2, bottom: -2, trailing: 0))
    }
}

#Preview {
    GroupSettingsView(groupId: "g1").environmentObject(AppModel())
}
