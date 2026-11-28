import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 组队/语音合并面板(08 稿 token 化,P4d):两个副平面一个 sheet,避免
/// toolbar 图标挤爆。游戏在线状态偏好已移去设备面板(与 android/09 稿
/// 同位)。任一面失败静默降级——notice 行提示,其余段照常。
struct PartyVoiceView: View {
    @EnvironmentObject var model: AppModel
    @State private var inviteTarget = ""
    @State private var roomName = ""
    @State private var joinRoomId = ""

    var body: some View {
        NavigationStack {
            List {
                partyInvitesSection
                partySection
                voiceSection
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("组队与语音")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Theme.bg, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        model.refreshAuxiliaryPlanes()
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
    }

    @Environment(\.dismiss) private var dismiss

    // ---- 组队邀请 -------------------------------------------------------------

    @ViewBuilder private var partyInvitesSection: some View {
        if !model.partyInvites.isEmpty {
            Section {
                ForEach(model.partyInvites, id: \.inviteId) { invite in
                    HStack(spacing: 12) {
                        AvatarView(label: invite.fromUserId, size: Theme.avatarSm)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(invite.fromUserId)
                                .font(Theme.fontBodySemibold)
                                .foregroundStyle(Theme.textPrimary)
                            if !invite.partyId.isEmpty {
                                Text("队伍 \(invite.partyId)")
                                    .font(Theme.fontMeta)
                                    .foregroundStyle(Theme.textTertiary)
                            }
                        }
                        Spacer(minLength: 8)
                        Button("拒绝") {
                            model.respondToPartyInviteTapped(
                                inviteId: invite.inviteId, accept: false)
                        }
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.danger)
                        .buttonStyle(.plain)
                        Button("加入") {
                            model.respondToPartyInviteTapped(
                                inviteId: invite.inviteId, accept: true)
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
            } header: {
                sectionHeader("组队邀请")
            }
        }
    }

    // ---- 组队 ----------------------------------------------------------------

    private var partySection: some View {
        Section {
            if let notice = model.planesNotice {
                Text(notice)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
            if let party = model.partySnapshot {
                partyRows(party)
            } else {
                Button {
                    model.createPartyTapped()
                } label: {
                    Text("创建队伍")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(ChirpButtonStyle(kind: .primary))
                .listRowBackground(Color.clear)
                .listRowSeparator(.hidden)
            }
        } header: {
            sectionHeader("组队")
        } footer: {
            sectionFooter("队伍全员共享快照;就绪状态由队长视角汇总。")
        }
    }

    @ViewBuilder private func partyRows(_ party: PartyIndex.Snapshot) -> some View {
        HStack {
            Text("队伍 \(party.partyId)")
                .font(Theme.fontTitle)
                .foregroundStyle(Theme.textPrimary)
            Spacer()
            Text("\(party.members.count)/\(max(party.maxMembers, 1)) 人")
                .font(Theme.fontMeta)
                .foregroundStyle(Theme.textTertiary)
        }
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
        ForEach(party.members, id: \.userId) { member in
            HStack(spacing: 12) {
                AvatarView(label: member.userId, size: Theme.avatarSm)
                Text(member.userId)
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .lineLimit(1)
                if member.userId == party.leaderId {
                    ChipText(text: "队长", tone: .game)
                }
                Spacer(minLength: 8)
                if member.userId == model.currentUserId {
                    Button(
                        member.ready ? "取消就绪" : "就绪"
                    ) {
                        model.togglePartyReadyTapped()
                    }
                    .font(Theme.fontSecondary)
                    .foregroundStyle(member.ready ? Theme.textSecondary : Theme.accentSoft)
                    .buttonStyle(.plain)
                } else if member.ready {
                    ChipText(text: "已准备", tone: .live)
                }
            }
            .padding(.vertical, 2)
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        }
        HStack(spacing: 8) {
            TextField("对方用户 ID", text: $inviteTarget)
                .font(Theme.fontBody)
                .foregroundStyle(Theme.textPrimary)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .chirpField()
            Button {
                model.inviteToPartyTapped(targetUserId: inviteTarget)
                inviteTarget = ""
            } label: {
                Text("邀请")
                    .frame(width: 64)
            }
            .buttonStyle(ChirpButtonStyle(kind: .ghost))
            .disabled(inviteTarget.trimmingCharacters(in: .whitespaces).isEmpty)
        }
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
        Button {
            model.leavePartyTapped()
        } label: {
            Text("退队")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(ChirpButtonStyle(kind: .danger))
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
    }

    // ---- 语音房间 ------------------------------------------------------------

    private var voiceSection: some View {
        Section {
            if let room = model.voiceRoom {
                voiceRows(room)
            } else {
                HStack(spacing: 8) {
                    TextField("房间名(可空)", text: $roomName)
                        .font(Theme.fontBody)
                        .foregroundStyle(Theme.textPrimary)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .chirpField()
                    Button {
                        model.createVoiceRoomTapped(name: roomName)
                        roomName = ""
                    } label: {
                        Text("创建")
                            .frame(width: 64)
                    }
                    .buttonStyle(ChirpButtonStyle(kind: .ghost))
                }
                .listRowBackground(Color.clear)
                .listRowSeparator(.hidden)
                HStack(spacing: 8) {
                    TextField("房间 ID", text: $joinRoomId)
                        .font(Theme.fontBody)
                        .foregroundStyle(Theme.textPrimary)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .chirpField()
                    Button {
                        model.joinVoiceRoomTapped(roomId: joinRoomId)
                        joinRoomId = ""
                    } label: {
                        Text("加入")
                            .frame(width: 64)
                    }
                    .buttonStyle(ChirpButtonStyle(kind: .ghost))
                    .disabled(joinRoomId.trimmingCharacters(in: .whitespaces).isEmpty)
                }
                .listRowBackground(Color.clear)
                .listRowSeparator(.hidden)
            }
        } header: {
            sectionHeader("语音房间")
        } footer: {
            sectionFooter("信令/名册面:展示谁在房与各自状态,音频编解码属游戏客户端媒体面。")
        }
    }

    @ViewBuilder private func voiceRows(_ room: VoiceIndex.RoomSnapshot) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(room.roomName.isEmpty ? "语音房间" : room.roomName)
                    .font(Theme.fontTitle)
                    .foregroundStyle(Theme.textPrimary)
                Text(room.roomId)
                    .font(Theme.fontMeta)
                    .foregroundStyle(Theme.textTertiary)
            }
            Spacer()
            Text("\(room.participants.count) 人")
                .font(Theme.fontMeta)
                .foregroundStyle(Theme.textTertiary)
        }
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
        ForEach(room.participants, id: \.userId) { participant in
            HStack(spacing: 12) {
                AvatarView(label: participant.userId, size: Theme.avatarSm)
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        Text(participant.userId)
                            .font(Theme.fontBody)
                            .foregroundStyle(Theme.textPrimary)
                            .lineLimit(1)
                        if participant.userId == model.currentUserId {
                            Text("我")
                                .font(Theme.fontMeta)
                                .foregroundStyle(Theme.textTertiary)
                        }
                    }
                    Text(stateText(participant.state))
                        .font(Theme.fontMeta)
                        .foregroundStyle(Theme.textTertiary)
                }
                Spacer(minLength: 8)
                // 图标徽记:拒听(danger)优先于闭麦(warn),与 08 稿语义一致。
                if participant.deafened == true {
                    Image(systemName: "speaker.slash")
                        .font(.system(size: 13))
                        .foregroundStyle(Theme.danger)
                } else if participant.muted == true {
                    Image(systemName: "mic.slash")
                        .font(.system(size: 13))
                        .foregroundStyle(Theme.warn)
                }
                if participant.state == .connected {
                    ChipText(text: "已连接", tone: .live)
                }
            }
            .padding(.vertical, 2)
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        }
        HStack(spacing: 10) {
            Button {
                model.toggleVoiceMuteTapped()
            } label: {
                Label("闭麦/开麦", systemImage: "mic")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .ghost))
            Button {
                model.toggleVoiceDeafenTapped()
            } label: {
                Label("关听/开听", systemImage: "speaker")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(ChirpButtonStyle(kind: .ghost))
        }
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
        Button {
            model.leaveVoiceRoomTapped()
        } label: {
            Text("离开房间")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(ChirpButtonStyle(kind: .danger))
        .listRowBackground(Color.clear)
        .listRowSeparator(.hidden)
    }

    private func stateText(_ state: ChirpProtos.Chirp_Voice_ParticipantState) -> String {
        switch state {
        case .joining: return "加入中"
        case .connected: return "已连接"
        case .muted: return "闭麦"
        case .deafened: return "关听"
        case .disconnected: return "已离开"
        default: return "未知"
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
    PartyVoiceView().environmentObject(AppModel())
}
