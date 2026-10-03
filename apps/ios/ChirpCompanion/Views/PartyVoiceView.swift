import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 组队/语音/游戏状态合并面板(P4d):三个副平面一个 sheet,避免 toolbar
/// 图标挤爆。任一面失败静默降级——notice 行提示,其余段照常。
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
                gamePresenceSection
            }
            .navigationTitle("组队与语音")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("刷新") {
                        model.refreshAuxiliaryPlanes()
                        model.loadGamePresence()
                    }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button("完成") { dismiss() }
                }
            }
        }
        .onAppear { model.loadGamePresence() }
    }

    @Environment(\.dismiss) private var dismiss

    // ---- 组队 ----------------------------------------------------------------

    @ViewBuilder private var partyInvitesSection: some View {
        if !model.partyInvites.isEmpty {
            Section("组队邀请") {
                ForEach(model.partyInvites, id: \.inviteId) { invite in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(invite.fromUserId).font(.headline)
                            if !invite.partyId.isEmpty {
                                Text("队伍 \(invite.partyId)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        Spacer()
                        Button("拒绝") {
                            model.respondToPartyInviteTapped(
                                inviteId: invite.inviteId, accept: false)
                        }
                        .font(.subheadline)
                        .buttonStyle(.borderless)
                        Button("加入") {
                            model.respondToPartyInviteTapped(
                                inviteId: invite.inviteId, accept: true)
                        }
                        .font(.subheadline)
                        .buttonStyle(.borderless)
                    }
                }
            }
        }
    }

    private var partySection: some View {
        Section {
            if let notice = model.planesNotice {
                Text(notice)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            if let party = model.partySnapshot {
                partyRows(party)
            } else {
                Button("创建队伍") { model.createPartyTapped() }
            }
        } header: {
            Text("组队")
        } footer: {
            Text("队伍全员共享快照;就绪状态由队长视角汇总。")
        }
    }

    @ViewBuilder private func partyRows(_ party: PartyIndex.Snapshot) -> some View {
        HStack {
            Text("队伍 \(party.partyId)").font(.headline)
            Spacer()
            Text("\(party.members.count)/\(max(party.maxMembers, 1)) 人")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        ForEach(party.members, id: \.userId) { member in
            HStack {
                if member.userId == party.leaderId {
                    Image(systemName: "crown")
                        .font(.caption)
                        .foregroundStyle(.orange)
                }
                Text(member.userId)
                if member.userId == model.currentUserId {
                    Spacer()
                    Text("我")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Button(
                        member.ready ? "取消就绪" : "就绪"
                    ) {
                        model.togglePartyReadyTapped()
                    }
                    .font(.subheadline)
                    .buttonStyle(.borderless)
                } else if member.ready {
                    Spacer()
                    Image(systemName: "checkmark.circle.fill")
                        .font(.caption)
                        .foregroundStyle(.green)
                }
            }
        }
        HStack {
            TextField("对方用户 ID", text: $inviteTarget)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
            Button("邀请") {
                model.inviteToPartyTapped(targetUserId: inviteTarget)
                inviteTarget = ""
            }
            .disabled(inviteTarget.trimmingCharacters(in: .whitespaces).isEmpty)
        }
        Button("退队", role: .destructive) { model.leavePartyTapped() }
    }

    // ---- 语音房间 ------------------------------------------------------------

    private var voiceSection: some View {
        Section {
            if let room = model.voiceRoom {
                voiceRows(room)
            } else {
                HStack {
                    TextField("房间名(可空)", text: $roomName)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("创建") {
                        model.createVoiceRoomTapped(name: roomName)
                        roomName = ""
                    }
                }
                HStack {
                    TextField("房间 ID", text: $joinRoomId)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("加入") {
                        model.joinVoiceRoomTapped(roomId: joinRoomId)
                        joinRoomId = ""
                    }
                    .disabled(joinRoomId.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }
        } header: {
            Text("语音房间")
        } footer: {
            Text("信令/名册面:展示谁在房与各自状态,音频编解码属游戏客户端媒体面。")
        }
    }

    @ViewBuilder private func voiceRows(_ room: VoiceIndex.RoomSnapshot) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(room.roomName.isEmpty ? "语音房间" : room.roomName)
                    .font(.headline)
                Text(room.roomId)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Text("\(room.participants.count) 人")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        ForEach(room.participants, id: \.userId) { participant in
            HStack {
                Image(systemName: "person.wave.2")
                    .font(.caption)
                    .foregroundStyle(
                        participant.state == .connected ? .green : .secondary)
                Text(participant.userId)
                if participant.userId == model.currentUserId {
                    Text("我")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                if participant.deafened == true {
                    Image(systemName: "speaker.slash")
                        .font(.caption)
                        .foregroundStyle(.red)
                }
                if participant.muted == true {
                    Image(systemName: "mic.slash")
                        .font(.caption)
                        .foregroundStyle(.orange)
                }
                Text(stateText(participant.state))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        HStack {
            Button("闭麦/开麦") { model.toggleVoiceMuteTapped() }
                .buttonStyle(.borderless)
            Button("关听/开听") { model.toggleVoiceDeafenTapped() }
                .buttonStyle(.borderless)
        }
        .font(.subheadline)
        Button("离开房间", role: .destructive) { model.leaveVoiceRoomTapped() }
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

    // ---- 游戏在线状态 ---------------------------------------------------------

    private var gamePresenceSection: some View {
        Section {
            if let presence = model.gamePresenceState {
                Toggle("对外展示游戏在线状态", isOn: Binding(
                    get: { presence.enabled },
                    set: { model.setGamePresenceEnabledTapped($0) }))
                if presence.gameIds.isEmpty {
                    Text(presence.enabled ? "暂无已绑定的游戏" : "开关关闭时不展示")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(presence.gameIds, id: \.self) { gameId in
                        Text(gameId)
                    }
                }
            } else {
                Text("游戏状态面不可用(设备面未就绪)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        } header: {
            Text("游戏在线状态")
        } footer: {
            Text("开关只冻结对外广播;游戏绑定关系由游戏侧维护。")
        }
    }
}

#Preview {
    PartyVoiceView().environmentObject(AppModel())
}
