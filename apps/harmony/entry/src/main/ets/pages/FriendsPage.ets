import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import { PresenceStatus } from '@chirp/proto/social';
import { appServices } from '../common/AppServices';
import { TabBar, TopBar, Avatar, PillButton, Sheet } from '../common/widgets.ets';
import { C, R, S, ChatRouteParams } from '../common/Tokens';
import { privateKey } from '../code/state/models';
import { upsertConversation } from '../code/state/conversation_store';

/** 好友请求行（friend_store pendingIn 的窄化形态；ArkTS 不用索引访问类型）。 */
interface FriendRequestRow {
  requestId: string;
  fromUserId: string;
}

/**
 * 好友页（tab）：请求处理 + 好友名单 + 在线点 + 添加好友。游戏好友段
 * 不做——数据面（好友的游戏在线聚合）未实现，如实留空。social 平面
 * 不可用时整页降级为一条说明（聊天不受影响）。
 */
@Entry
@Component
struct FriendsPage {
  @State friends: string[] = [];
  @State pendingIn: FriendRequestRow[] = [];
  @State pendingOut: string[] = [];
  @State unavailable: boolean = false;
  @State selfId: string = '';
  @State showAdd: boolean = false;
  @State addInput: string = '';
  @State addError: string = '';
  @State busy: boolean = false;
  private now: number = Date.now();
  private unsubs: Array<() => void> = [];
  private tickTimer: number = -1;

  build() {
    Stack() {
      Column() {
      TopBar({
        title: '好友',
        rightText: '添加',
        onRight: () => this.openAdd(),
      })
      if (this.unavailable) {
        this.UnavailableNote()
        Blank()
      } else {
        List() {
          if (this.pendingIn.length > 0) {
            this.SectionHeader('好友请求')
            ForEach(this.pendingIn, (req: FriendRequestRow) => {
              ListItem() {
                this.RequestRow(req)
              }
            }, (req: FriendRequestRow) => req.requestId)
          }
          if (this.pendingOut.length > 0) {
            ListItem() {
              Text(`等待对方同意:${this.pendingOut.join('、')}`)
                .fontSize(12)
                .fontColor(C.text3)
                .width('100%')
                .padding({ left: 16, right: 16, top: 8, bottom: 8 })
            }
          }
          this.SectionHeader(`好友 · ${this.friends.length}`)
          ForEach(this.friends, (id: string) => {
            ListItem() {
              this.FriendRow(id)
            }
          }, (id: string) => id)
          if (this.friends.length === 0 && this.pendingIn.length === 0) {
            ListItem() {
              Text('还没有好友,输入对方用户 ID 发送请求。')
                .fontSize(13)
                .fontColor(C.text3)
                .width('100%')
                .padding(24)
                .textAlign(TextAlign.Center)
            }
          }
        }
        .layoutWeight(1)
        .scrollBar(BarState.Off)
        .divider({ strokeWidth: 1, color: C.borderSoft, startMargin: 72, endMargin: 16 })
      }
      TabBar({ active: 'friends' })
      }
      .width('100%')
      .height('100%')

      if (this.showAdd) {
        Sheet({
          title: '添加好友',
          onCancel: () => this.closeAdd(),
          content: () => {
            TextInput({ placeholder: '对方用户 ID', text: this.addInput })
              .type(InputType.Normal)
              .fontSize(15)
              .fontColor(C.text)
              .placeholderColor(C.text3)
              .backgroundColor(C.surface2)
              .borderRadius(R.row)
              .height(48)
              .width('100%')
              .onChange((v: string) => {
                this.addInput = v;
                this.addError = '';
              })
            if (this.addError !== '') {
              Text(this.addError)
                .fontSize(13)
                .fontColor(C.danger)
                .margin({ top: 10 })
            }
          },
          actions: () => {
            Column({ space: 10 }) {
              PillButton({
                label: this.busy ? '发送中…' : '发送请求',
                enabled: !this.busy && this.addInput.trim() !== '',
                onTap: () => void this.confirmAdd(),
              })
              PillButton({ label: '取消', enabled: true, onTap: () => this.closeAdd() })
            }
          },
        })
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }

  private closeAdd(): void {
    this.showAdd = false;
    this.addError = '';
  }

  private openAdd(): void {
    this.addInput = '';
    this.addError = '';
    this.showAdd = true;
  }

  @Builder
  private UnavailableNote() {
    Text('社交服务未连接')
      .fontSize(13)
      .fontColor(C.text3)
      .width('100%')
      .padding(20)
      .textAlign(TextAlign.Center)
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
  private RequestRow(req: FriendRequestRow) {
    Row({ space: 12 }) {
      Avatar({ name: req.fromUserId, size: S.avatarMd })
      Text(`${req.fromUserId} 请求加你为好友`)
        .fontSize(14)
        .fontColor(C.text)
        .layoutWeight(1)
        .maxLines(2)
      Text('接受')
        .fontSize(13)
        .fontColor(C.accentSoft)
        .padding(8)
        .onClick(() => void this.respond(req.requestId, true))
      Text('拒绝')
        .fontSize(13)
        .fontColor(C.text2)
        .padding(8)
        .onClick(() => void this.respond(req.requestId, false))
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
  }

  @Builder
  private FriendRow(id: string) {
    Row({ space: 12 }) {
      Stack({ alignContent: Alignment.BottomEnd }) {
        Avatar({ name: id, size: S.avatarMd })
        if (this.isOnline(id)) {
          Circle()
            .width(12)
            .height(12)
            .fill(C.online)
            .stroke(C.bg)
            .strokeWidth(2)
        }
      }
      Column({ space: 2 }) {
        Text(id)
          .fontSize(15)
          .fontWeight(FontWeight.Medium)
          .fontColor(C.text)
        Text(this.isOnline(id) ? '在线' : '离线')
          .fontSize(12)
          .fontColor(this.isOnline(id) ? C.online : C.text3)
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      Text('发消息')
        .fontSize(13)
        .fontColor(C.accentSoft)
        .padding({ left: 10, right: 10, top: 6, bottom: 6 })
        .borderRadius(R.pill)
        .backgroundColor(C.accentDim)
        .onClick(() => this.openChat(id))
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 10, bottom: 10 })
    .gesture(
      LongPressGesture().onAction(() => this.confirmRemove(id)),
    )
  }

  aboutToAppear(): void {
    const services = appServices.services;
    if (!services || !services.socialApi) {
      this.unavailable = true;
      return;
    }
    this.selfId = services.auth.get().userId ?? '';
    this.refreshLocal();
    this.unsubs = [
      services.friends.subscribe(() => this.refreshLocal()),
      services.auth.subscribe(() => {
        this.selfId = services.auth.get().userId ?? '';
      }),
    ];
    this.tickTimer = setInterval(() => {
      this.now = Date.now();
    }, 30_000);
    void services.socialApi.fetchFriends();
    void services.socialApi.fetchPending();
  }

  aboutToDisappear(): void {
    if (this.tickTimer >= 0) clearInterval(this.tickTimer);
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private refreshLocal(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.friends.get();
    this.friends = [...state.friends];
    this.pendingIn = state.pendingIn.map((r) => {
      const row: FriendRequestRow = { requestId: r.requestId, fromUserId: r.fromUserId };
      return row;
    });
    this.pendingOut = [...state.pendingOut];
  }

  private isOnline(id: string): boolean {
    const services = appServices.services;
    if (!services) return false;
    const entry = services.presence.get().byUser[id];
    if (!entry || this.now - entry.at >= 70_000) return false;
    return entry.status === PresenceStatus.ONLINE;
  }

  private async respond(requestId: string, accept: boolean): Promise<void> {
    const services = appServices.services;
    if (!services || !services.socialApi) return;
    await services.socialApi.respondRequest(requestId, accept);
    this.refreshLocal();
  }

  private async confirmAdd(): Promise<void> {
    const target = this.addInput.trim();
    const services = appServices.services;
    if (this.busy || target === '' || !services || !services.socialApi) return;
    if (target === this.selfId) {
      this.addError = '不能添加自己';
      return;
    }
    this.busy = true;
    try {
      const code = await services.socialApi.addFriend(target);
      if (code === 0) {
        this.showAdd = false;
      } else {
        this.addError = '添加失败(可能已是好友)';
      }
    } catch {
      this.addError = '添加失败(可能已是好友)';
    } finally {
      this.busy = false;
    }
  }

  private confirmRemove(id: string): void {
    promptAction.showActionMenu({
      buttons: [{ text: '删除好友', color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      void services?.socialApi?.removeFriend(id);
    }).catch(() => {});
  }

  private openChat(id: string): void {
    const services = appServices.services;
    if (!services || this.selfId === '') return;
    const key = privateKey(this.selfId, id);
    upsertConversation(services.conversations, {
      kind: 'private',
      key,
      channelId: [this.selfId, id].sort().join('|'),
      peerId: id,
      title: id,
      unreadLocal: 0,
    });
    const params = new ChatRouteParams();
    params.key = key;
    router.pushUrl({ url: 'pages/ChatPage', params: params }).catch(() => {});
  }
}
