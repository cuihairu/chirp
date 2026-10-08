import promptAction from '@ohos.promptAction';
import { ParticipantState } from '@chirp/proto/voice';
import { appServices } from '../common/AppServices';
import { TabBar, TopBar, PillButton, Sheet } from '../common/widgets.ets';
import { C, R } from '../common/Tokens';
import { isLeaderOf, selfMemberOf } from '../code/state/party_store';
import { selfParticipantOf } from '../code/state/voice_store';
import type { PartySnapshot } from '../code/state/party_store';
import type { VoiceParticipant } from '../code/state/voice_store';

/** 成员行（party snapshot / voice roster 的窄化形态）。 */
interface MemberRow {
  userId: string;
  tag: string;
  tagColor: string;
}

/**
 * 组队/语音页（tab）：segmented 两段。组队 = 队伍快照 + 邀请处理 +
 * 创建/邀请/准备/移交/解散；语音 = 房间名单 + 创建/加入/静音/拒听/退出。
 * 两平面均为协议面（无 WebRTC 媒体，对齐 web 端边界）；任一平面不可用
 * 时该段降级说明，聊天不受影响。
 */
@Entry
@Component
struct PartyVoicePage {
  @State segment: string = 'party';
  @State party: PartySnapshot | null = null;
  @State invites: Array<{ inviteId: string; fromUserId: string; partyId: string }> = [];
  @State partyUnavailable: boolean = false;
  @State room: { roomId: string; roomName: string; maxParticipants: number; participants: VoiceParticipant[] } | null = null;
  @State voiceUnavailable: boolean = false;
  @State selfId: string = '';
  @State showSheet: boolean = false;
  @State sheetKind: string = 'party-create';
  @State input: string = '';
  @State sheetError: string = '';
  @State busy: boolean = false;
  private unsubs: Array<() => void> = [];

  build() {
    Stack() {
      Column() {
        TopBar({ title: this.segment === 'party' ? '组队' : '语音' })
        Row({ space: 8 }) {
          this.SegmentTab('party', '组队')
          this.SegmentTab('voice', '语音')
        }
        .margin({ left: 16, right: 16, top: 4 })
        if (this.segment === 'party') {
          this.PartyPane()
        } else {
          this.VoicePane()
        }
        TabBar({ active: 'party' })
      }
      .width('100%')
      .height('100%')

      if (this.showSheet) {
        Sheet({
          title: this.sheetTitle(),
          onCancel: () => this.closeSheet(),
          content: () => {
            if (this.sheetKind !== 'party-create-max') {
              TextInput({ placeholder: this.sheetPlaceholder(), text: this.input })
                .type(InputType.Normal)
                .fontSize(15)
                .fontColor(C.text)
                .placeholderColor(C.text3)
                .backgroundColor(C.surface2)
                .borderRadius(R.row)
                .height(48)
                .width('100%')
                .onChange((v: string) => {
                  this.input = v;
                  this.sheetError = '';
                })
            }
            if (this.sheetError !== '') {
              Text(this.sheetError)
                .fontSize(13)
                .fontColor(C.danger)
                .margin({ top: 10 })
            }
          },
          actions: () => {
            Column({ space: 10 }) {
              PillButton({
                label: this.busy ? '处理中…' : this.sheetConfirm(),
                enabled: !this.busy && (this.input.trim() !== '' || this.sheetKind === 'party-create' || this.sheetKind === 'voice-create'),
                onTap: () => void this.confirmSheet(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeSheet() })
            }
          },
        })
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }

  @Builder
  private SegmentTab(key: string, label: string) {
    Text(label)
      .fontSize(14)
      .fontWeight(FontWeight.Medium)
      .fontColor(this.segment === key ? C.text : C.text2)
      .textAlign(TextAlign.Center)
      .height(34)
      .layoutWeight(1)
      .borderRadius(R.pill)
      .backgroundColor(this.segment === key ? C.accent : C.surface2)
      .onClick(() => {
        this.segment = key;
      })
  }

  @Builder
  private PartyPane() {
    Scroll() {
      Column() {
        if (this.partyUnavailable) {
          this.Note('组队服务未连接')
        } else if (this.party) {
          this.MemberCard(this.partyRows(), `成员 ${this.party.members.length}/${this.party.maxMembers || 5}`)
          Column({ space: 10 }) {
            if (this.selfReady()) {
              PillButton({ label: '取消准备', enabled: !this.busy, onTap: () => void this.setReady(false) })
            } else {
              PillButton({ label: '准备', enabled: !this.busy, onTap: () => void this.setReady(true) })
            }
            if (isLeaderOf({ party: this.party, invites: this.invites }, this.selfId)) {
              PillButton({ label: '邀请', enabled: !this.busy, onTap: () => this.openSheet('party-invite') })
              PillButton({ label: '解散队伍', enabled: !this.busy, destructive: true, onTap: () => this.confirmDisband() })
            }
            PillButton({ label: '退出队伍', enabled: !this.busy, destructive: true, onTap: () => this.confirmLeaveParty() })
          }
          .margin({ top: 14 })
        } else {
          this.Note('还没有加入队伍,创建一个或等待邀请。')
          Column({ space: 10 }) {
            PillButton({ label: '创建队伍', enabled: !this.busy, onTap: () => this.openSheet('party-create') })
          }
          .margin({ top: 8 })
        }
        if (this.invites.length > 0) {
          Text('组队邀请')
            .fontSize(12)
            .fontColor(C.text3)
            .width('100%')
            .margin({ top: 16, bottom: 6 })
          ForEach(this.invites, (inv: { inviteId: string; fromUserId: string; partyId: string }) => {
            Column({ space: 8 }) {
              Text(`${inv.fromUserId} 邀请你组队`)
                .fontSize(14)
                .fontColor(C.text)
              Row({ space: 10 }) {
                Text('接受')
                  .fontSize(13)
                  .fontColor(C.accentSoft)
                  .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                  .borderRadius(R.pill)
                  .backgroundColor(C.accentDim)
                  .onClick(() => void this.respondInvite(inv.inviteId, true))
                Text('拒绝')
                  .fontSize(13)
                  .fontColor(C.text2)
                  .padding({ left: 10, right: 10, top: 6, bottom: 6 })
                  .borderRadius(R.pill)
                  .backgroundColor(C.surface2)
                  .onClick(() => void this.respondInvite(inv.inviteId, false))
              }
            }
            .width('100%')
            .padding(12)
            .backgroundColor(C.surface)
            .borderRadius(R.card)
            .margin({ bottom: 8 })
          }, (inv: { inviteId: string; fromUserId: string; partyId: string }) => inv.inviteId)
        }
      }
      .width('100%')
      .padding({ left: 16, right: 16, bottom: 16 })
    }
    .layoutWeight(1)
    .scrollBar(BarState.Off)
    .align(Alignment.Top)
  }

  @Builder
  private VoicePane() {
    Scroll() {
      Column() {
        if (this.voiceUnavailable) {
          this.Note('语音服务未连接')
        } else if (this.room) {
          this.MemberCard(this.voiceRows(), `成员 ${this.room.participants.length}/${this.room.maxParticipants || '∞'}`)
          Column({ space: 10 }) {
            if (this.selfMuted()) {
              PillButton({ label: '取消静音', enabled: !this.busy, onTap: () => void this.setMute(false) })
            } else {
              PillButton({ label: '静音自己', enabled: !this.busy, onTap: () => void this.setMute(true) })
            }
            if (this.selfDeafened()) {
              PillButton({ label: '取消拒听', enabled: !this.busy, onTap: () => void this.setDeafen(false) })
            } else {
              PillButton({ label: '拒听(听不到别人)', enabled: !this.busy, onTap: () => void this.setDeafen(true) })
            }
            PillButton({ label: '退出房间', enabled: !this.busy, destructive: true, onTap: () => this.confirmLeaveVoice() })
          }
          .margin({ top: 14 })
          Text('注:此为协议面名单(房间/成员/静音态),音频媒体面未接入。')
            .fontSize(11)
            .fontColor(C.text3)
            .width('100%')
            .margin({ top: 6 })
        } else {
          this.Note('未在语音房间中,创建或加入一个。')
          Column({ space: 10 }) {
            PillButton({ label: '创建房间', enabled: !this.busy, onTap: () => this.openSheet('voice-create') })
            PillButton({ label: '加入房间', enabled: !this.busy, onTap: () => this.openSheet('voice-join') })
          }
          .margin({ top: 8 })
        }
      }
      .width('100%')
      .padding({ left: 16, right: 16, bottom: 16 })
    }
    .layoutWeight(1)
    .scrollBar(BarState.Off)
    .align(Alignment.Top)
  }

  @Builder
  private MemberCard(rows: MemberRow[], header: string) {
    Column({ space: 8 }) {
      Text(header)
        .fontSize(12)
        .fontColor(C.text3)
        .width('100%')
      ForEach(rows, (m: MemberRow) => {
        Row({ space: 10 }) {
          Text(m.userId)
            .fontSize(14)
            .fontColor(C.text)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .layoutWeight(1)
          if (m.tag !== '') {
            Text(m.tag)
              .fontSize(11)
              .fontColor(m.tagColor)
              .padding({ left: 6, right: 6, top: 1, bottom: 1 })
              .borderRadius(R.pill)
              .backgroundColor(C.accentDim)
          }
        }
        .width('100%')
        .padding(10)
        .backgroundColor(C.surface2)
        .borderRadius(R.row)
        .gesture(
          LongPressGesture().onAction(() => this.onMemberLongPress(m)),
        )
      }, (m: MemberRow) => `${m.userId}:${m.tag}`)
    }
    .width('100%')
    .padding(14)
    .backgroundColor(C.surface)
    .borderRadius(R.card)
  }

  @Builder
  private Note(text: string) {
    Text(text)
      .fontSize(13)
      .fontColor(C.text3)
      .width('100%')
      .padding(12)
  }

  aboutToAppear(): void {
    const services = appServices.services;
    if (!services) return;
    this.selfId = services.auth.get().userId ?? '';
    this.partyUnavailable = services.partyApi === null;
    this.voiceUnavailable = services.voiceApi === null;
    this.refreshLocal();
    this.unsubs = [
      services.partyState.subscribe(() => this.refreshPartyLocal()),
      services.voiceState.subscribe(() => this.refreshVoiceLocal()),
    ];
    void services.partyApi?.fetchMyParty();
  }

  aboutToDisappear(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private refreshLocal(): void {
    this.refreshPartyLocal();
    this.refreshVoiceLocal();
  }

  private refreshPartyLocal(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.partyState.get();
    this.party = state.party ? { ...state.party, members: state.party.members.map((m) => ({ ...m })) } : null;
    this.invites = state.invites.map((i) => ({ inviteId: i.inviteId, fromUserId: i.fromUserId, partyId: i.partyId }));
    this.partyUnavailable = services.partyApi === null;
  }

  private refreshVoiceLocal(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.voiceState.get();
    this.room = state.room
      ? {
          roomId: state.room.roomId,
          roomName: state.room.roomName,
          maxParticipants: state.room.maxParticipants,
          participants: state.room.participants.map((p) => ({ ...p })),
        }
      : null;
    this.voiceUnavailable = services.voiceApi === null;
  }

  private partyRows(): MemberRow[] {
    if (!this.party) return [];
    return this.party.members.map((m) => {
      let tag = '';
      let tagColor = C.text2;
      if (m.userId === this.party?.leaderId) {
        tag = '队长';
        tagColor = C.accentSoft;
      } else if (m.ready) {
        tag = '已准备';
        tagColor = C.online;
      }
      if (m.userId === this.selfId) {
        tag = `${tag} 我`.trim();
      }
      const row: MemberRow = { userId: m.userId, tag, tagColor };
      return row;
    });
  }

  private voiceRows(): MemberRow[] {
    if (!this.room) return [];
    return this.room.participants.map((p) => {
      let tag = '';
      let tagColor = C.text2;
      if (p.state === ParticipantState.DEAFENED) {
        tag = '拒听';
      } else if (p.state === ParticipantState.MUTED) {
        tag = '已静音';
      } else if (p.state === ParticipantState.CONNECTED) {
        tag = '通话中';
        tagColor = C.online;
      }
      if (p.userId === this.selfId) {
        tag = `${tag} 我`.trim();
      }
      const row: MemberRow = { userId: p.userId, tag, tagColor };
      return row;
    });
  }

  private selfReady(): boolean {
    if (!this.selfId || !this.party) return false;
    return selfMemberOf({ party: this.party, invites: this.invites }, this.selfId)?.ready === true;
  }

  private selfMuted(): boolean {
    return this.selfSelf()?.muted === true;
  }

  private selfDeafened(): boolean {
    return this.selfSelf()?.deafened === true;
  }

  private selfSelf(): VoiceParticipant | undefined {
    if (!this.selfId || !this.room) return undefined;
    return selfParticipantOf({ room: this.room }, this.selfId);
  }

  private onMemberLongPress(m: MemberRow): void {
    // 组队段：队长可移出/转让；语音段无管理动作。
    if (this.segment !== 'party' || !this.party) return;
    const leader = this.party.leaderId === this.selfId;
    if (!leader || m.userId === this.selfId) return;
    promptAction.showActionMenu({
      buttons: [
        { text: '移出', color: C.danger },
        { text: '转让队长', color: C.text },
      ],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      const services = appServices.services;
      if (!services || !services.partyApi) return;
      if (res.index === 0) void services.partyApi.kickMember(m.userId);
      if (res.index === 1) void services.partyApi.transferLeader(m.userId);
    }).catch(() => {});
  }

  private openSheet(kind: string): void {
    this.sheetKind = kind;
    this.input = '';
    this.sheetError = '';
    this.showSheet = true;
  }

  private closeSheet(): void {
    this.showSheet = false;
    this.sheetError = '';
  }

  private sheetTitle(): string {
    if (this.sheetKind === 'party-create') return '创建队伍';
    if (this.sheetKind === 'party-invite') return '邀请';
    if (this.sheetKind === 'voice-create') return '创建房间';
    return '加入房间';
  }

  private sheetPlaceholder(): string {
    if (this.sheetKind === 'party-create') return '人数上限(留空默认 5)';
    if (this.sheetKind === 'party-invite') return '对方用户 ID';
    if (this.sheetKind === 'voice-create') return '房间名(留空默认)';
    return '房间 ID';
  }

  private sheetConfirm(): string {
    if (this.sheetKind === 'party-create') return '创建';
    if (this.sheetKind === 'party-invite') return '邀请';
    if (this.sheetKind === 'voice-create') return '创建';
    return '加入';
  }

  private async confirmSheet(): Promise<void> {
    const services = appServices.services;
    if (this.busy || !services) return;
    this.busy = true;
    try {
      if (this.sheetKind === 'party-create') {
        const max = Number.parseInt(this.input.trim(), 10);
        const code = await services.partyApi?.createParty(Number.isNaN(max) ? 0 : max);
        if (code === 0) this.closeSheet();
        else this.sheetError = '创建队伍失败';
      } else if (this.sheetKind === 'party-invite') {
        const code = await services.partyApi?.invite(this.input.trim());
        if (code === 0) this.closeSheet();
        else this.sheetError = '邀请失败';
      } else if (this.sheetKind === 'voice-create') {
        const code = await services.voiceApi?.createRoom(this.input.trim());
        if (code === 0) this.closeSheet();
        else this.sheetError = '创建房间失败';
      } else {
        const code = await services.voiceApi?.joinRoom(this.input.trim());
        if (code === 0) this.closeSheet();
        else this.sheetError = '加入房间失败';
      }
      this.refreshLocal();
    } catch {
      this.sheetError = '操作失败,请稍后再试';
    } finally {
      this.busy = false;
    }
  }

  private async respondInvite(inviteId: string, accept: boolean): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    if (accept) await services.partyApi?.acceptInvite(inviteId);
    else await services.partyApi?.declineInvite(inviteId);
    this.refreshPartyLocal();
  }

  private async setReady(ready: boolean): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    await services.partyApi?.setReady(ready);
    this.refreshPartyLocal();
  }

  private confirmDisband(): void {
    promptAction.showActionMenu({
      buttons: [{ text: '解散整个队伍?', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      void services?.partyApi?.disbandParty().then(() => this.refreshPartyLocal());
    }).catch(() => {});
  }

  private confirmLeaveParty(): void {
    promptAction.showActionMenu({
      buttons: [{ text: '退出这个队伍?', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      void services?.partyApi?.leaveParty().then(() => this.refreshPartyLocal());
    }).catch(() => {});
  }

  private confirmLeaveVoice(): void {
    promptAction.showActionMenu({
      buttons: [{ text: '退出这个语音房间?', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      void services?.voiceApi?.leaveRoom().then(() => this.refreshVoiceLocal());
    }).catch(() => {});
  }

  private async setMute(muted: boolean): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    await services.voiceApi?.setMute(muted);
    this.refreshVoiceLocal();
  }

  private async setDeafen(deafened: boolean): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    await services.voiceApi?.setDeafen(deafened);
    this.refreshVoiceLocal();
  }
}
