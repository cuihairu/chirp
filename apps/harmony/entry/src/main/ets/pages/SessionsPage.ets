import router from '@ohos.router';
import { PresenceStatus } from '@chirp/proto/social';
import type { ConnStatus } from '@chirp/app-protocol/chirp_client';
import { appServices } from '../common/AppServices';
import { TabBar, TopBar, Avatar, PillButton, Sheet } from '../common/widgets.ets';
import { C, R, S, ChatRouteParams, conversationTime } from '../common/Tokens';
import { privateKey, groupKey, type Conversation } from '../code/state/models';
import { upsertConversation } from '../code/state/conversation_store';

/**
 * 会话列表页（tab 首页）：私聊/群聊分组、未读徽标、在线点；顶部两个主
 * 操作（新的私聊/新建群组）走全屏 sheet——服务端没有私聊列表 API（P1
 * 聚合是权威修法），先手填对方 ID 建会话，与 web 端一致。
 */
@Entry
@Component
struct SessionsPage {
  @State conversations: Conversation[] = [];
  @State selfId: string = '';
  @State kicked: boolean = false;
  @State connStatus: ConnStatus = 'idle';
  @State showNewChat: boolean = false;
  @State showNewGroup: boolean = false;
  @State peerIdInput: string = '';
  @State groupNameInput: string = '';
  @State sheetError: string = '';
  @State busy: boolean = false;
  private now: number = Date.now();
  private unsubs: Array<() => void> = [];
  private lastPresencePullKey: string = '';

  build() {
    Stack() {
      Column() {
        if (this.kicked) {
          this.KickedBanner()
        } else if (this.connStatus === 'waiting-reconnect' || this.connStatus === 'connecting') {
          this.ReconnectBanner()
        }
        TopBar({
          title: 'Chirp 伴侣',
          subtitle: this.selfId === '' ? '' : `欢迎,${this.selfId}`,
          showBell: true,
          onBell: () => router.pushUrl({ url: 'pages/NotificationsPage' }).catch(() => {}),
          avatarName: this.selfId,
          onAvatar: () => router.pushUrl({ url: 'pages/ProfilePage' }).catch(() => {}),
          rightText: '退出登录',
          onRight: () => void this.onSignOut(),
        })
        if (this.conversations.length === 0) {
          Column({ space: 10 }) {
            Text('💬').fontSize(40)
            Text('选择左侧会话,或发起新的私聊。')
              .fontSize(14)
              .fontColor(C.text2)
          }
          .width('100%')
          .layoutWeight(1)
          .justifyContent(FlexAlign.Center)
        } else {
          List() {
            this.SectionHeader('私聊', this.privates.length)
            ForEach(this.privates, (c: Conversation) => {
              ListItem() {
                this.ConvRow(c)
              }
            }, (c: Conversation) => c.key)
            this.SectionHeader('群聊', this.groups.length)
            ForEach(this.groups, (c: Conversation) => {
              ListItem() {
                this.ConvRow(c)
              }
            }, (c: Conversation) => c.key)
          }
          .layoutWeight(1)
          .scrollBar(BarState.Off)
          .divider({
            strokeWidth: 1,
            color: C.borderSoft,
            startMargin: 72,
            endMargin: 16,
          })
        }
        Row({ space: 10 }) {
          Button('新的私聊')
            .fontSize(14)
            .fontColor(C.text)
            .backgroundColor(C.surface3)
            .borderRadius(R.pill)
            .height(40)
            .layoutWeight(1)
            .onClick(() => this.openSheet('newChat'))
          Button('新建群组')
            .fontSize(14)
            .fontColor(C.text)
            .backgroundColor(C.surface3)
            .borderRadius(R.pill)
            .height(40)
            .layoutWeight(1)
            .onClick(() => this.openSheet('newGroup'))
        }
        .padding({ left: 16, right: 16, top: 10, bottom: 10 })
        TabBar({ active: 'sessions' })
      }
      .width('100%')
      .height('100%')

      if (this.showNewChat) {
        Sheet({
          title: '发起私聊',
          onCancel: () => this.closeSheets(),
          content: () => {
            this.SheetInput('对方用户 ID', this.peerIdInput, (v: string) => {
              this.peerIdInput = v;
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
                label: this.busy ? '连接中…' : '开始聊天',
                enabled: !this.busy && this.peerIdInput.trim() !== '',
                onTap: () => void this.confirmNewChat(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeSheets() })
            }
          },
        })
      }
      if (this.showNewGroup) {
        Sheet({
          title: '创建群组',
          onCancel: () => this.closeSheets(),
          content: () => {
            this.SheetInput('群组名称', this.groupNameInput, (v: string) => {
              this.groupNameInput = v;
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
                enabled: !this.busy && this.groupNameInput.trim() !== '',
                onTap: () => void this.confirmNewGroup(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeSheets() })
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
  private SectionHeader(label: string, count: number) {
    if (count > 0) {
      Text(`${label} · ${count}`)
        .fontSize(12)
        .fontColor(C.text3)
        .width('100%')
        .padding({ left: 16, top: 12, bottom: 6 })
    }
  }

  @Builder
  private ConvRow(c: Conversation) {
    Row({ space: 12 }) {
      Stack({ alignContent: Alignment.BottomEnd }) {
        Avatar({ name: c.title, size: S.avatarMd })
        if (c.kind === 'private' && this.isOnline(c.peerId)) {
          Circle()
            .width(12)
            .height(12)
            .fill(C.online)
            .stroke(C.bg)
            .strokeWidth(2)
        }
      }
      Column({ space: 3 }) {
        Row({ space: 8 }) {
          Text(c.title)
            .fontSize(15)
            .fontWeight(FontWeight.Medium)
            .fontColor(C.text)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .layoutWeight(1)
          Text(conversationTime(c.lastMessageAt))
            .fontSize(11)
            .fontColor(C.text3)
        }
        .width('100%')
        Row({ space: 8 }) {
          Text(c.lastMessagePreview ?? (c.kind === 'group' ? '新群组' : '开始聊天'))
            .fontSize(13)
            .fontColor(c.lastMessagePreview ? C.text2 : C.text3)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .layoutWeight(1)
          if (c.unreadLocal > 0) {
            Text(c.unreadLocal > 99 ? '99+' : `${c.unreadLocal}`)
              .fontSize(11)
              .fontColor(C.text)
              .backgroundColor(C.accent)
              .borderRadius(10)
              .padding({ left: 6, right: 6, top: 1, bottom: 1 })
          }
        }
        .width('100%')
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
    .onClick(() => this.openConversation(c.key))
  }

  @Builder
  private ReconnectBanner() {
    Text('连接已断开,正在自动重连…')
      .fontSize(13)
      .fontColor(C.warn)
      .width('100%')
      .padding(8)
      .textAlign(TextAlign.Center)
      .backgroundColor(C.surface2)
  }

  @Builder
  private KickedBanner() {
    Row({ space: 10 }) {
      Text('账号已在其他设备登录,当前会话已断开')
        .fontSize(13)
        .fontColor(C.danger)
        .layoutWeight(1)
      Text('返回登录')
        .fontSize(13)
        .fontColor(C.accentSoft)
        .onClick(() => void this.onSignOut())
    }
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
    .backgroundColor(C.surface2)
  }

  @Builder
  private SheetInput(placeholder: string, value: string, onChange: (v: string) => void) {
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

  private get privates(): Conversation[] {
    return this.conversations.filter((c) => c.kind === 'private');
  }

  private get groups(): Conversation[] {
    return this.conversations.filter((c) => c.kind === 'group');
  }

  private isOnline(peerId: string): boolean {
    const services = appServices.services;
    if (!services) return false;
    const entry = services.presence.get().byUser[peerId];
    if (!entry || this.now - entry.at >= 70_000) return false;
    return entry.status === PresenceStatus.ONLINE;
  }

  aboutToAppear(): void {
    const services = appServices.services;
    if (!services) {
      // 直接深跳进来（未建服务面）：回配置页。
      router.replaceUrl({ url: 'pages/HostPage' }).catch(() => {});
      return;
    }
    this.selfId = services.auth.get().userId ?? '';
    this.kicked = services.auth.get().kicked;
    this.connStatus = services.client.status;
    this.refreshLocal();
    this.unsubs = [
      services.conversations.subscribe(() => this.refreshLocal()),
      services.auth.subscribe(() => {
        const auth = services.auth.get();
        this.selfId = auth.userId ?? '';
        this.kicked = auth.kicked;
      }),
      services.client.onStatus((status: ConnStatus) => {
        this.connStatus = status;
      }),
    ];
    // 群名单 bootstrap：重登后 create/invite 类 notify 全部错过，靠拉取兜底。
    void services.api.refreshGroups();
    void services.socialApi?.pullPresence(this.privatePeers());
  }

  aboutToDisappear(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private refreshLocal(): void {
    const services = appServices.services;
    if (!services) return;
    this.conversations = [...services.conversations.get().conversations];
    const ids = this.privatePeers();
    const pullKey = ids.join(',');
    if (services.socialApi && pullKey !== '' && pullKey !== this.lastPresencePullKey) {
      this.lastPresencePullKey = pullKey;
      void services.socialApi.pullPresence(ids);
    }
  }

  private privatePeers(): string[] {
    return this.conversations
      .filter((c) => c.kind === 'private')
      .map((c) => c.peerId);
  }

  private openSheet(kind: string): void {
    this.sheetError = '';
    this.peerIdInput = '';
    this.groupNameInput = '';
    if (kind === 'newChat') this.showNewChat = true;
    else this.showNewGroup = true;
  }

  private closeSheets(): void {
    this.showNewChat = false;
    this.showNewGroup = false;
    this.sheetError = '';
  }

  private async confirmNewChat(): Promise<void> {
    const peer = this.peerIdInput.trim();
    const services = appServices.services;
    if (this.busy || peer === '' || !services || !this.selfId) return;
    if (peer === this.selfId) {
      this.sheetError = '不能和自己私聊';
      return;
    }
    const key = privateKey(this.selfId, peer);
    upsertConversation(services.conversations, {
      kind: 'private',
      key,
      channelId: [this.selfId, peer].sort().join('|'),
      peerId: peer,
      title: peer,
      unreadLocal: 0,
    });
    this.closeSheets();
    this.openConversation(key);
  }

  private async confirmNewGroup(): Promise<void> {
    const name = this.groupNameInput.trim();
    const services = appServices.services;
    if (this.busy || name === '' || !services) return;
    this.busy = true;
    try {
      const groupId = await services.api.createGroup(name);
      if (groupId) {
        this.closeSheets();
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

  private openConversation(key: string): void {
    const params = new ChatRouteParams();
    params.key = key;
    router.pushUrl({ url: 'pages/ChatPage', params: params }).catch(() => {});
  }

  private async onSignOut(): Promise<void> {
    await appServices.logoutAll();
    await router.replaceUrl({ url: 'pages/LoginPage' });
  }
}
