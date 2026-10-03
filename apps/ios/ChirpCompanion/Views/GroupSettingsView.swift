import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 群设置面板(P4e,web GroupSettingsDialog 对齐):添加成员(INVITE
/// 直接入群)、成员清单(群主标识、群主可移出他人)、退出群组(确认后
/// 退,sessionDropped 事件把聊天面弹回列表)。sheet 形态,完成即关。
struct GroupSettingsView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let groupId: String
    @State private var inviteTarget = ""
    @State private var confirmLeave = false

    private var ownerId: String? {
        model.groupRoster.first { $0.groupId == groupId }?.ownerId
    }

    private var amOwner: Bool {
        ownerId == model.currentUserId
    }

    var body: some View {
        NavigationStack {
            List {
                Section("添加成员") {
                    HStack {
                        TextField("成员用户 ID", text: $inviteTarget)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                        Button("添加") {
                            let target = inviteTarget
                            inviteTarget = ""
                            model.inviteToGroupTapped(
                                groupId: groupId, targetUserId: target)
                        }
                        .disabled(inviteTarget.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
                Section("成员") {
                    if let notice = model.groupPanelNotice {
                        Text(notice)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    ForEach(model.groupMembers, id: \.userID) { member in
                        memberRow(member)
                    }
                }
                Section {
                    Button("退出群组", role: .destructive) {
                        confirmLeave = true
                    }
                }
            }
            .navigationTitle("群组设置:\(model.groupName(groupId))")
            .navigationBarTitleDisplayMode(.inline)
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
            .onAppear { model.openGroupPanel(groupId: groupId) }
        }
    }

    private func memberRow(_ member: Chirp_Chat_GroupMember) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(member.username.isEmpty ? member.userID : member.username)
                if member.userID == ownerId {
                    Text("群主")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
            Spacer()
            // 只有群主能移出成员,且不指向自己(web 同款)。
            if amOwner && member.userID != model.currentUserId {
                Button("移出", role: .destructive) {
                    model.kickGroupMemberTapped(
                        groupId: groupId, targetUserId: member.userID)
                }
                .font(.subheadline)
            }
        }
    }
}

#Preview {
    GroupSettingsView(groupId: "g1").environmentObject(AppModel())
}
