import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import { appServices } from '../common/AppServices';
import { TopBar, Avatar, PillButton, Sheet } from '../common/widgets.ets';
import {
  C,
  R,
  S,
  QUICK_REACTIONS,
  ChatRouteParams,
  GroupRouteParams,
  hhmm,
} from '../common/Tokens';
import { channelRefOf, privatePeerId } from '../code/api/chat_api';
import type { ChannelRef } from '../code/api/chat_api';
import { conversationOf, type ChatMessageView, type MessageReactionView } from '../code/state/models';
import { clearUnread } from '../code/state/conversation_store';
import { messagesOf, readCursorOf } from '../code/state/message_store';
import { typingUsersOf } from '../code/state/typing_store';

// 与 web 端 ChatWindow.tsx 同源同一组的时序常量。
const TYPING_START_INTERVAL_MS = 3000;
const TYPING_IDLE_MS = 5000;
const TYPING_TICK_MS = 1000;

/**
 * 聊天页（web 端 ChatWindow 的移动形态）：消息流 + 输入栏 + 正在输入 +
 * 已读回执 + 表情回应 + 编辑/撤回 + 加载更早。群频道右上角进群设置。
 */
@Entry
@Component
struct ChatPage {
  @State messages: ChatMessageView[] = [];
  @State title: string = '';
  @State subtitle: string = '';
  @State kind: string = 'private';
  @State channelId: string = '';
  @State peerId: string = '';
  @State selfId: string = '';
  @State hasMore: boolean = false;
  @State loadingEarlier: boolean = false;
  @State typists: string[] = [];
  @State draft: string = '';
  @State editingId: string = '';
  @State reactMessageId: string = '';
  @State showReactSheet: boolean = false;
  private now: number = Date.now();
  private channelKey: string = '';
  private ref: ChannelRef | null = null;
  private historyLoaded: boolean = false;
  private lastTypingStartAt: number = 0;
  private typingIdleTimer: number = -1;
  private unsubs: Array<() => void> = [];
  private tickTimer: number = -1;
  private scroller: Scroller = new Scroller();

  build() {
    Stack() {
      Column() {
        TopBar({
          title: this.title,
          subtitle: this.subtitle,
          showBack: true,
          onBack: () => router.back(),
          rightText: this.kind === 'group' ? '群设置' : '',
          onRight: () => this.openGroupSettings(),
        })
        if (this.hasMore) {
          Text(this.loadingEarlier ? '加载中…' : '加载更早的消息')
            .fontSize(13)
            .fontColor(C.accentSoft)
            .width('100%')
            .padding(8)
            .textAlign(TextAlign.Center)
            .onClick(() => void this.loadEarlier())
        }
        Scroll(this.scroller) {
          Column() {
            if (this.messages.length === 0) {
              Text('选择左侧会话,或发起新的私聊。')
                .fontSize(13)
                .fontColor(C.text3)
                .margin({ top: 48 })
            }
            ForEach(this.messages, (m: ChatMessageView, index: number) => {
              this.MessageRow(m, index > 0 ? this.messages[index - 1] : null)
            }, (m: ChatMessageView) => m.messageId)
          }
          .width('100%')
          .padding({ left: 12, right: 12, top: 8, bottom: 12 })
        }
        .layoutWeight(1)
        .scrollBar(BarState.Off)
        .align(Alignment.Top)
        if (this.editingId !== '') {
          Row({ space: 10 }) {
            Text('编辑消息')
              .fontSize(12)
              .fontColor(C.warn)
            Blank()
            Text('取消')
              .fontSize(13)
              .fontColor(C.text2)
              .onClick(() => this.cancelEdit())
          }
          .width('100%')
          .padding({ left: 16, right: 16, top: 8 })
        }
        Row({ space: 10 }) {
          TextInput({ placeholder: '输入消息…', text: this.draft })
            .type(InputType.Normal)
            .fontSize(15)
            .fontColor(C.text)
            .placeholderColor(C.text3)
            .backgroundColor(C.surface2)
            .borderRadius(R.pill)
            .height(42)
            .layoutWeight(1)
            .enterKeyType(EnterKeyType.Send)
            .onChange((value: string) => this.onDraftChange(value))
            .onSubmit(() => void this.onSend())
          Button('↑')
            .fontSize(18)
            .fontColor(C.text)
            .backgroundColor(this.draft.trim() === '' ? C.surface3 : C.accent)
            .width(42)
            .height(42)
            .borderRadius(21)
            .onClick(() => void this.onSend())
        }
        .padding({ left: 12, right: 12, top: 8, bottom: 10 })
      }
      .width('100%')
      .height('100%')

      if (this.showReactSheet) {
        Sheet({
          title: '添加表情',
          onCancel: () => this.closeReactSheet(),
          content: () => {
            Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Center }) {
              ForEach(QUICK_REACTIONS, (emoji: string) => {
                Text(emoji)
                  .fontSize(26)
                  .width(52)
                  .height(52)
                  .textAlign(TextAlign.Center)
                  .backgroundColor(C.surface2)
                  .borderRadius(26)
                  .margin(6)
                  .onClick(() => void this.toggleReaction(this.reactMessageId, emoji))
              }, (emoji: string) => emoji)
            }
            .width('100%')
          },
          actions: () => {
            PillButton({ label: '关闭', enabled: true, onTap: () => this.closeReactSheet() })
          },
        })
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }

  @Builder
  private MessageRow(m: ChatMessageView, prev: ChatMessageView | null) {
    if (m.deleted) {
      Text('消息已撤回')
        .fontSize(12)
        .fontColor(C.text3)
        .width('100%')
        .textAlign(TextAlign.Center)
        .padding({ top: 6, bottom: 6 })
    } else {
      Column({ space: 3 }) {
        if (this.kind === 'group' && m.senderId !== this.selfId && prev?.senderId !== m.senderId) {
          Text(m.senderId)
            .fontSize(11)
            .fontColor(C.text3)
            .margin({ left: 12 })
        }
        Row() {
          if (m.senderId !== this.selfId && (prev === null || prev.senderId !== m.senderId)) {
            Avatar({ name: m.senderId, size: S.avatarSm })
              .margin({ right: 6 })
          } else if (m.senderId !== this.selfId) {
            Blank().width(S.avatarSm + 6)
          }
          Column({ space: 3 }) {
            Column() {
              Text(m.content)
                .fontSize(15)
                .fontColor(C.text)
                .lineHeight(21)
            }
            .backgroundColor(m.senderId === this.selfId ? C.accentStrong : C.surface2)
            .borderRadius(R.row)
            .padding({ left: 12, right: 12, top: 8, bottom: 8 })
            .constraintSize({ maxWidth: '78%' })
            if ((m.reactions !== undefined && Object.keys(m.reactions).length > 0)) {
              Flex({ wrap: FlexWrap.Wrap }) {
                ForEach(this.reactionList(m), (r: MessageReactionView) => {
                  Text(`${r.emoji} ${r.count}`)
                    .fontSize(12)
                    .fontColor(r.mine ? C.accentSoft : C.text2)
                    .backgroundColor(r.mine ? C.accentDim : C.surface3)
                    .borderRadius(R.pill)
                    .padding({ left: 8, right: 8, top: 2, bottom: 2 })
                    .margin({ right: 6, top: 2 })
                    .onClick(() => void this.toggleReaction(m.messageId, r.emoji))
                }, (r: MessageReactionView) => `${r.emoji}:${r.count}:${r.mine}`)
              }
            }
            this.StatusLine(m)
          }
          .alignItems(m.senderId === this.selfId ? HorizontalAlign.End : HorizontalAlign.Start)
          .layoutWeight(1)
        }
        .width('100%')
      }
      .width('100%')
      .margin({ top: 4 })
      .gesture(
        LongPressGesture()
          .onAction(() => this.onLongPress(m)),
      )
    }
  }

  @Builder
  private StatusLine(m: ChatMessageView) {
    Row({ space: 6 }) {
      Text(hhmm(m.timestamp))
        .fontSize(10)
        .fontColor(C.text3)
      if (m.senderId === this.selfId) {
        if (m.pending) {
          Text('发送中…').fontSize(10).fontColor(C.text3)
        } else if (m.failed) {
          Text('发送失败').fontSize(10).fontColor(C.danger)
        } else if (m.queuedOffline) {
          Text('对方离线,已排队').fontSize(10).fontColor(C.warn)
        } else if (this.isReadByPeer(m)) {
          Text('已读').fontSize(10).fontColor(C.accentSoft)
        }
        if (m.edited) {
          Text('(已编辑)').fontSize(10).fontColor(C.text3)
        }
      }
    }
    .padding({ left: 4, right: 4 })
  }

  private reactionList(m: ChatMessageView): MessageReactionView[] {
    return Object.values(m.reactions ?? {});
  }

  private isReadByPeer(m: ChatMessageView): boolean {
    const services = appServices.services;
    if (!services || this.channelKey === '') return false;
    const peer = this.kind === 'private' ? this.peerId : null;
    if (!peer) return false;
    const cursor = readCursorOf(services.messages.get(), this.channelKey, peer);
    return cursor !== undefined && cursor >= m.messageId;
  }

  aboutToAppear(): void {
    const services = appServices.services;
    const params = router.getParams() as ChatRouteParams | undefined;
    this.channelKey = params?.key ?? '';
    if (!services || this.channelKey === '') {
      router.back();
      return;
    }
    const auth = services.auth.get();
    this.selfId = auth.userId ?? '';
    this.ref = channelRefOf(this.channelKey, this.selfId);
    const { kind, channelId } = conversationOf(this.channelKey);
    this.kind = kind;
    this.channelId = channelId;
    this.peerId = this.ref.peerId;
    this.refreshHeader();
    services.api.setActiveChannel(this.channelKey);
    clearUnread(services.conversations, this.channelKey);
    this.unsubs = [
      services.messages.subscribe(() => this.refreshMessages()),
      services.typing.subscribe(() => this.refreshTyping()),
      services.conversations.subscribe(() => this.refreshHeader()),
    ];
    this.tickTimer = setInterval(() => {
      this.now = Date.now();
      this.refreshTyping();
    }, TYPING_TICK_MS);
    this.refreshMessages();
    void this.bootstrapChannel();
  }

  aboutToDisappear(): void {
    const services = appServices.services;
    if (services) services.api.setActiveChannel(null);
    if (this.typingIdleTimer >= 0) clearTimeout(this.typingIdleTimer);
    if (this.tickTimer >= 0) clearInterval(this.tickTimer);
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private refreshHeader(): void {
    const services = appServices.services;
    if (!services) return;
    const conv = services.conversations.get().conversations.find((c) => c.key === this.channelKey);
    if (conv) {
      this.title = conv.title;
      if (this.kind === 'group') {
        this.peerId = conv.peerId;
        this.subtitle = '群聊';
      } else {
        this.subtitle = '';
      }
    } else {
      this.title = this.kind === 'private' ? privatePeerId(this.channelId, this.selfId) : this.channelId;
      this.subtitle = '';
    }
  }

  private refreshMessages(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.messages.get();
    this.messages = [...messagesOf(state, this.channelKey)];
    this.hasMore = state.hasMore[this.channelKey] === true;
    const last = this.messages[this.messages.length - 1];
    if (last && !last.pending && !last.failed) {
      void services.api.markRead(this.refOrThrow(), last.messageId);
    }
    setTimeout(() => this.scroller.scrollEdge(Edge.Bottom), 30);
  }

  private refreshTyping(): void {
    const services = appServices.services;
    if (!services) return;
    const users = typingUsersOf(services.typing.get(), this.channelKey, this.now);
    const joined = users.join('、');
    const before = this.typists.join('、');
    if (joined !== before) {
      this.typists = users;
      this.subtitle = users.length > 0 ? `${joined} 正在输入…` : '';
    }
  }

  private async bootstrapChannel(): Promise<void> {
    const services = appServices.services;
    if (!services || this.historyLoaded) return;
    this.historyLoaded = true;
    if (this.messages.length === 0) {
      await services.api.loadHistory(this.refOrThrow());
      this.refreshMessages();
    }
    const newest = this.messages[this.messages.length - 1];
    await services.api.markRead(this.refOrThrow(), newest ? newest.messageId : '');
    this.scroller.scrollEdge(Edge.Bottom);
  }

  private refOrThrow(): ChannelRef {
    if (!this.ref) {
      throw new Error('ChatPage: channel ref missing');
    }
    return this.ref;
  }

  private async loadEarlier(): Promise<void> {
    const services = appServices.services;
    const oldest = this.messages[0];
    if (!services || this.loadingEarlier || !oldest || this.hasMore === false) return;
    this.loadingEarlier = true;
    try {
      await services.api.loadHistory(this.refOrThrow(), oldest.timestamp);
      this.refreshMessages();
    } finally {
      this.loadingEarlier = false;
    }
  }

  private onDraftChange(value: string): void {
    this.draft = value;
    const services = appServices.services;
    if (!services || value.trim() === '') return;
    // 节流对齐 web：3s 才再发一次 start，5s 无输入回 stop。
    const now = Date.now();
    if (now - this.lastTypingStartAt >= TYPING_START_INTERVAL_MS) {
      this.lastTypingStartAt = now;
      services.api.sendTyping(this.refOrThrow(), true);
    }
    if (this.typingIdleTimer >= 0) clearTimeout(this.typingIdleTimer);
    this.typingIdleTimer = setTimeout(() => {
      services.api.sendTyping(this.refOrThrow(), false);
      this.lastTypingStartAt = 0;
    }, TYPING_IDLE_MS);
  }

  private async onSend(): Promise<void> {
    const services = appServices.services;
    const text = this.draft.trim();
    if (!services || text === '') return;
    if (this.editingId !== '') {
      const id = this.editingId;
      this.cancelEdit();
      await services.api.editMessage(id, text);
      return;
    }
    this.draft = '';
    if (this.typingIdleTimer >= 0) clearTimeout(this.typingIdleTimer);
    services.api.sendTyping(this.refOrThrow(), false);
    this.lastTypingStartAt = 0;
    await services.api.sendMessage(this.refOrThrow(), text);
    this.refreshMessages();
  }

  private onLongPress(m: ChatMessageView): void {
    const mine = m.senderId === this.selfId;
    const buttons: Array<promptAction.Button> = [{ text: '添加表情', color: C.text }];
    if (mine) {
      buttons.push({ text: '编辑', color: C.text });
      buttons.push({ text: '删除', color: C.danger });
    }
    promptAction
      .showActionMenu({ buttons: buttons })
      .then((res: promptAction.ActionMenuSuccessResponse) => {
        if (res.index === 0) {
          this.reactMessageId = m.messageId;
          this.showReactSheet = true;
        } else if (res.index === 1 && mine) {
          this.editingId = m.messageId;
          this.draft = m.content;
        } else if (res.index === 2 && mine) {
          this.confirmDelete(m.messageId);
        }
      })
      .catch(() => {
        // 菜单被取消
      });
  }

  private confirmDelete(messageId: string): void {
    AlertDialog.show({
      message: '撤回这条消息?',
      autoCancel: true,
      alignment: DialogAlignment.Center,
      primaryButton: { value: '取消', action: () => {} },
      secondaryButton: {
        value: '撤回',
        fontColor: C.danger,
        action: () => void this.doDelete(messageId),
      },
    });
  }

  private async doDelete(messageId: string): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    await services.api.deleteMessage(messageId);
    this.refreshMessages();
  }

  private cancelEdit(): void {
    this.editingId = '';
    this.draft = '';
  }

  private async toggleReaction(messageId: string, emoji: string): Promise<void> {
    const services = appServices.services;
    if (!services) return;
    const msg = this.messages.find((m) => m.messageId === messageId);
    const mine = msg?.reactions?.[emoji]?.mine === true;
    if (mine) {
      await services.api.removeReaction(messageId, emoji);
    } else {
      await services.api.addReaction(messageId, emoji);
    }
    this.refreshMessages();
  }

  private closeReactSheet(): void {
    this.showReactSheet = false;
    this.reactMessageId = '';
  }

  private openGroupSettings(): void {
    if (this.kind !== 'group') return;
    const params = new GroupRouteParams();
    params.groupId = this.channelId;
    router.pushUrl({ url: 'pages/GroupPage', params: params }).catch(() => {});
  }
}
