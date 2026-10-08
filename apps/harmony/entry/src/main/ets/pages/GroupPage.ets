import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import { appServices } from '../common/AppServices';
import { TopBar, Avatar, PillButton, Sheet } from '../common/widgets.ets';
import { C, R, S, ChatRouteParams, GroupRouteParams, conversationTime } from '../common/Tokens';
import { groupKey } from '../code/state/models';
import type { Conversation } from '../code/state/models';

/** 群成员行（GET_GROUP_MEMBERS 响应的窄化形态）。 */
interface GroupMemberRow {
  userId: string;
  role: string;
}

/**
 * 群页（无参数 = 名单/新建模式，从会话页「新建群组」进入；带 groupId =
 * 群设置模式，从聊天页 ⚙ 进入）：成员名单、添加成员、移出成员、退出
 * 群组。踢人仅群主可见（ownerId 由会话条目携带，服务端为权威）。
 */
@Entry
@Component
struct GroupPage {
  @State groupId: string = '';
  @State title: string = '';
  @State ownerId: string = '';
  @State members: GroupMemberRow[] = [];
  @State groups: Conversation[] = [];
  @State selfId: string = '';
  @State showCreate: boolean = false;
  @State showInvite: boolean = false;
  @State nameInput: string = '';
  @State inviteInput: string = '';
  @State sheetError: string = '';
  @State busy: boolean = false;
  private unsubs: Array<() => void> = [];

  get isDetail(): boolean {
    return this.groupId !== '';
  }

  get amOwner(): boolean {
    return this.ownerId === this.selfId;
  }

  build() {
    Stack() {
      Column() {
        TopBar({
          title: this.isDetail ? (this.title === '' ? this.groupId : this.title) : '群组',
          subtitle: this.isDetail ? `成员 ${this.members.length}` : '',
          showBack: true,
          onBack: () => router.back(),
        })
        if (this.isDetail) {
          List() {
            this.SectionHeader('成员')
            ForEach(this.members, (m: GroupMemberRow) => {
              ListItem() {
                this.MemberRow(m)
              }
            }, (m: GroupMemberRow) => m.userId)
            if (this.members.length === 0) {
              ListItem() {
                Text('成员名单加载中…')
                  .fontSize(13)
                  .fontColor(C.text3)
                  .width('100%')
                  .padding(20)
                  .textAlign(TextAlign.Center)
              }
            }
            ListItem() {
              Column({ space: 10 }) {
                PillButton({
                  label: '添加成员',
                  enabled: !this.busy,
                  onTap: () => this.openInvite(),
                })
                PillButton({
                  label: '退出群组',
                  enabled: !this.busy,
                  destructive: true,
                  onTap: () => this.confirmLeave(),
                })
              }
              .margin({ top: 20 })
            }
          }
          .layoutWeight(1)
          .scrollBar(BarState.Off)
          .padding({ top: 4 })
        } else {
          if (this.groups.length === 0) {
            Column({ space: 10 }) {
              Text('👥').fontSize(40)
              Text('还没有群组,创建一个。')
                .fontSize(13)
                .fontColor(C.text3)
            }
            .width('100%')
            .layoutWeight(1)
            .justifyContent(FlexAlign.Center)
          } else {
            List() {
              ForEach(this.groups, (c: Conversation) => {
                ListItem() {
                  this.GroupRow(c)
                }
              }, (c: Conversation) => c.key)
            }
            .layoutWeight(1)
            .scrollBar(BarState.Off)
            .divider({ strokeWidth: 1, color: C.borderSoft, startMargin: 72, endMargin: 16 })
          }
          Row() {
            PillButton({
              label: '新建群组',
              enabled: !this.busy,
              onTap: () => this.openCreate(),
            })
          }
          .padding({ left: 16, right: 16, top: 10, bottom: 10 })
        }
      }
      .width('100%')
      .height('100%')

      if (this.showCreate) {
        Sheet({
          title: '创建群组',
          onCancel: () => this.closeCreate(),
          content: () => {
            this.TextInput2('群组名称', this.nameInput, (v: string) => {
              this.nameInput = v;
              this.sheetError = '';
            })
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
                label: this.busy ? '创建中…' : '创建',
                enabled: !this.busy && this.nameInput.trim() !== '',
                onTap: () => void this.confirmCreate(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeCreate() })
            }
          },
        })
      }
      if (this.showInvite) {
        Sheet({
          title: '添加成员',
          onCancel: () => this.closeInvite(),
          content: () => {
            this.TextInput2('成员用户 ID', this.inviteInput, (v: string) => {
              this.inviteInput = v;
              this.sheetError = '';
            })
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
                label: this.busy ? '添加中…' : '添加',
                enabled: !this.busy && this.inviteInput.trim() !== '',
                onTap: () => void this.confirmInvite(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeInvite() })
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
  private TextInput2(placeholder: string, value: string, onChange: (v: string) => void) {
    TextInput({ placeholder: placeholder, text: value })
      .type(InputType.Normal)
      .fontSize(15)
      .fontColor(C.text)
      .placeholderColor(C.text3)
      .backgroundColor(C.surface2)
      .borderRadius(R.row)
      .height(48)
      .width('100%')
      .onChange((v: string) => onChange(v))
  }

  @Builder
  private SectionHeader(label: string) {
    Text(label)
      .fontSize(12)
      .fontColor(C.text3)
      .width('100%')
      .padding({ left: 16, top: 12, bottom: 6 })
  }

  @Builder
  private GroupRow(c: Conversation) {
    Row({ space: 12 }) {
      Avatar({ name: c.title, size: S.avatarMd })
      Column({ space: 3 }) {
        Text(c.title)
          .fontSize(15)
          .fontWeight(FontWeight.Medium)
          .fontColor(C.text)
        Text(c.lastMessagePreview ?? '新群组')
          .fontSize(13)
          .fontColor(C.text2)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      Text(conversationTime(c.lastMessageAt))
        .fontSize(11)
        .fontColor(C.text3)
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
    .onClick(() => this.openConversation(c.key))
  }

  @Builder
  private MemberRow(m: GroupMemberRow) {
    Row({ space: 12 }) {
      Avatar({ name: m.userId, size: S.avatarSm })
      Text(m.userId)
        .fontSize(14)
        .fontColor(C.text)
        .layoutWeight(1)
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
      if (m.userId === this.ownerId) {
        Text('群主')
          .fontSize(11)
          .fontColor(C.accentSoft)
          .padding({ left: 6, right: 6, top: 2, bottom: 2 })
          .borderRadius(R.pill)
          .backgroundColor(C.accentDim)
      }
      if (m.userId === this.selfId) {
        Text('我')
          .fontSize(11)
          .fontColor(C.text3)
      }
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 8, bottom: 8 })
    .gesture(
      LongPressGesture().onAction(() => this.onMemberLongPress(m)),
    )
  }

  aboutToAppear(): void {
    const services = appServices.services;
    if (!services) {
      router.back();
      return;
    }
    const params = router.getParams() as GroupRouteParams | undefined;
    this.groupId = params?.groupId ?? '';
    this.selfId = services.auth.get().userId ?? '';
    if (this.isDetail) {
      this.refreshDetailHeader();
      void this.loadMembers();
    } else {
      this.refreshGroups();
      this.unsubs.push(services.conversations.subscribe(() => this.refreshGroups()));
    }
    this.unsubs.push(
      services.conversations.subscribe(() => {
        if (this.isDetail) this.refreshDetailHeader();
      }),
    );
  }

  aboutToDisappear(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private refreshGroups(): void {
    const services = appServices.services;
    if (!services) return;
    this.groups = services.conversations
      .get()
      .conversations.filter((c) => c.kind === 'group');
  }

  private refreshDetailHeader(): void {
    const services = appServices.services;
    if (!services) return;
    const conv = services.conversations.get().conversations.find((c) => c.key === groupKey(this.groupId));
    if (conv) {
      this.title = conv.title;
      this.ownerId = conv.ownerId ?? '';
    }
  }

  private async loadMembers(): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    try {
      const members = await services.api.loadGroupMembers(this.groupId);
      this.members = (members ?? []).map((m: GroupMemberRow) => {
        const row: GroupMemberRow = { userId: m.userId, role: m.role };
        return row;
      });
    } catch {
      this.members = [];
    }
  }

  private onMemberLongPress(m: GroupMemberRow): void {
    if (!this.amOwner || m.userId === this.selfId) return;
    promptAction.showActionMenu({
      buttons: [{ text: '移出', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      if (!services) return;
      void services.api.kickMember(this.groupId, m.userId);
    }).catch(() => {});
  }

  private openCreate(): void {
    this.nameInput = '';
    this.sheetError = '';
    this.showCreate = true;
  }

  private closeCreate(): void {
    this.showCreate = false;
    this.sheetError = '';
  }

  private openInvite(): void {
    this.inviteInput = '';
    this.sheetError = '';
    this.showInvite = true;
  }

  private closeInvite(): void {
    this.showInvite = false;
    this.sheetError = '';
  }

  private async confirmCreate(): Promise<void> {
    const name = this.nameInput.trim();
    const services = appServices.services;
    if (this.busy || name === '' || !services) return;
    this.busy = true;
    try {
      const groupId = await services.api.createGroup(name);
      if (groupId) {
        this.closeCreate();
        this.openConversation(groupKey(groupId));
      } else {
        this.sheetError = '创建群组失败';
      }
    } catch {
      this.sheetError = '创建群组失败';
    } finally {
      this.busy = false;
    }
  }

  private async confirmInvite(): Promise<void> {
    const target = this.inviteInput.trim();
    const services = appServices.services;
    if (this.busy || target === '' || !services) return;
    this.busy = true;
    try {
      const code = await services.api.inviteToGroup(this.groupId, target);
      if (code === 0) {
        this.closeInvite();
        await this.loadMembers();
      } else {
        this.sheetError = '添加成员失败';
      }
    } catch {
      this.sheetError = '添加成员失败';
    } finally {
      this.busy = false;
    }
  }

  private confirmLeave(): void {
    promptAction.showActionMenu({
      buttons: [{ text: '退出群组', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      void this.doLeave();
    }).catch(() => {});
  }

  private async doLeave(): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    await services.api.leaveGroup(this.groupId);
    await router.back();
  }

  private openConversation(key: string): void {
    const params = new ChatRouteParams();
    params.key = key;
    router.pushUrl({ url: 'pages/ChatPage', params: params }).catch(() => {});
  }
}
