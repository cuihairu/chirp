import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import { appServices } from '../common/AppServices';
import { TopBar } from '../common/widgets.ets';
import { C, R } from '../common/Tokens';
import { DND_START_MIN, DND_END_MIN } from '../common/NotifySettings';
import { clearUnread } from '../code/state/conversation_store';

/**
 * 通知中心页（参照 design/prototypes/mobile/04-notifications.html）：
 * 顶栏（返回 + "通知" + "全部已读"）→ 分组「今天/更早」通知流 → 推送设置卡。
 * 入口：会话列表顶栏铃铛（不占五页签，外部路由进入）。
 * 推送设置三开关接既有 Notify.ts/AppServices 数据面（NotifySettings.ts 持久化）；
 * 通知流历史一期为骨架——后端尚无通知历史拉取 API，未读角标取会话未读总和。
 */
/** 通知类别（ArkTS 不用索引访问类型，独立命名联合）。 */
type NotifCategory = 'message' | 'friend' | 'party' | 'system';

interface NotifItem {
  key: string;
  category: NotifCategory;
  title: string;
  body: string;
  /** 毫秒时间戳（用于今天/更早分组与时间显示）。 */
  at: number;
  unread: boolean;
}

@Entry
@Component
struct NotificationsPage {
  @State todayItems: NotifItem[] = [];
  @State earlierItems: NotifItem[] = [];
  @State unreadTotal: number = 0;
  @State systemPush: boolean = true;
  @State dnd: boolean = true;
  @State mentionOnly: boolean = false;
  private unsubsHolder: Array<() => void> = [];

  build() {
    Column() {
      TopBar({
        title: '通知',
        showBack: true,
        onBack: () => router.back(),
        rightText: this.unreadTotal > 0 ? '全部已读' : '',
        onRight: () => void this.markAllRead(),
      })
      Scroll() {
        Column() {
          this.SectionLabel('今天')
          if (this.todayItems.length > 0) {
            Column() {
              ForEach(this.todayItems, (n: NotifItem) => {
                this.NotifRow(n)
              }, (n: NotifItem) => n.key)
            }
            .width('100%')
            .backgroundColor(C.surface)
            .borderRadius(R.card)
          } else {
            this.EmptyHint('今天暂无通知')
          }
          this.SectionLabel('更早')
          if (this.earlierItems.length > 0) {
            Column() {
              ForEach(this.earlierItems, (n: NotifItem) => {
                this.NotifRow(n)
              }, (n: NotifItem) => n.key)
            }
            .width('100%')
            .backgroundColor(C.surface)
            .borderRadius(R.card)
          } else {
            this.EmptyHint('更早暂无通知')
          }
          this.PushSettingsCard()
        }
        .width('100%')
        .padding({ left: 16, right: 16, bottom: 24 })
      }
      .layoutWeight(1)
      .scrollBar(BarState.Off)
      .align(Alignment.Top)
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }

  @Builder
  private SectionLabel(label: string) {
    Text(label)
      .fontSize(12)
      .fontColor(C.text3)
      .width('100%')
      .margin({ top: 16, bottom: 6 })
  }

  @Builder
  private EmptyHint(text: string) {
    Text(text)
      .fontSize(13)
      .fontColor(C.text3)
      .width('100%')
      .textAlign(TextAlign.Center)
      .padding(16)
      .backgroundColor(C.surface)
      .borderRadius(R.card)
  }

  @Builder
  private NotifRow(n: NotifItem) {
    Row({ space: 12 }) {
      Row() {
        Text(this.iconFor(n.category))
          .fontSize(19)
      }
      .width(38)
      .height(38)
      .justifyContent(FlexAlign.Center)
      .alignItems(VerticalAlign.Center)
      .backgroundColor(C.surface2)
      .borderRadius(12)

      Column({ space: 2 }) {
        Text(n.title)
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .fontColor(C.text)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
        Text(n.body)
          .fontSize(13)
          .fontColor(C.text2)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      Column({ space: 4 }) {
        Text(this.labelFor(n.category))
          .fontSize(11)
          .fontColor(n.unread ? C.accent : C.text2)
          .padding({ left: 6, right: 6, top: 1, bottom: 1 })
          .borderRadius(R.pill)
          .backgroundColor(n.unread ? C.accentDim : C.surface2)
        Text(this.fmtClock(n.at))
          .fontSize(11)
          .fontColor(C.text3)
      }
    }
    .width('100%')
    .padding({ left: 14, right: 14, top: 12, bottom: 12 })
    .backgroundColor(n.unread ? C.accentDim : C.surface)
    .borderRadius(R.row)
  }

  @Builder
  private PushSettingsCard() {
    Column() {
      Row() {
        Text('推送设置')
          .fontSize(12)
          .fontColor(C.text3)
          .layoutWeight(1)
      }
      .width('100%')
      .padding({ top: 16, bottom: 6 })

      Column({ space: 0 }) {
        Row({ space: 10 }) {
          Column({ space: 2 }) {
            Text('系统推送').fontSize(14).fontColor(C.text)
            Text('经设备平面注册(RegisterDevice)').fontSize(11).fontColor(C.text3)
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          Toggle({ type: ToggleType.Switch, isOn: this.systemPush })
            .selectedColor(C.accent)
            .onChange((on: boolean) => {
              this.systemPush = on
              void appServices.setNotifySettings({
                systemPush: on,
                dnd: this.dnd,
                mentionOnly: this.mentionOnly,
              })
            })
        }
        .width('100%')
        .padding({ top: 10, bottom: 10 })

        Row({ space: 10 }) {
          Column({ space: 2 }) {
            Text('免打扰时段').fontSize(14).fontColor(C.text)
            Text(`${this.dndLabel()} 静默`).fontSize(11).fontColor(C.text3)
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          Toggle({ type: ToggleType.Switch, isOn: this.dnd })
            .selectedColor(C.accent)
            .onChange((on: boolean) => {
              this.dnd = on
              void appServices.setNotifySettings({
                systemPush: this.systemPush,
                dnd: on,
                mentionOnly: this.mentionOnly,
              })
            })
        }
        .width('100%')
        .padding({ top: 10, bottom: 10 })

        Row({ space: 10 }) {
          Column({ space: 2 }) {
            Text('仅好友与群提及时通知').fontSize(14).fontColor(C.text)
            Text('@提及与私聊除外').fontSize(11).fontColor(C.text3)
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          Toggle({ type: ToggleType.Switch, isOn: this.mentionOnly })
            .selectedColor(C.accent)
            .onChange((on: boolean) => {
              this.mentionOnly = on
              void appServices.setNotifySettings({
                systemPush: this.systemPush,
                dnd: this.dnd,
                mentionOnly: on,
              })
            })
        }
        .width('100%')
        .padding({ top: 10, bottom: 10 })
      }
      .width('100%')
      .padding(14)
      .backgroundColor(C.surface)
      .borderRadius(R.card)
    }
    .width('100%')
  }

  private iconFor(category: NotifCategory): string {
    if (category === 'message') return '💬'
    if (category === 'friend') return '👥'
    if (category === 'party') return '🎮'
    return '⚙'
  }

  private labelFor(category: NotifCategory): string {
    if (category === 'message') return '消息'
    if (category === 'friend') return '好友'
    if (category === 'party') return '组队'
    return '系统'
  }

  private fmtClock(at: number): string {
    const d = new Date(at)
    const h = d.getHours()
    const m = d.getMinutes()
    return `${h < 10 ? `0${h}` : `${h}`}:${m < 10 ? `0${m}` : `${m}`}`
  }

  private dndLabel(): string {
    const fmt = (min: number): string => {
      const h = Math.floor(min / 60)
      const m = min % 60
      return `${h < 10 ? `0${h}` : `${h}`}:${m < 10 ? `0${m}` : `${m}`}`
    }
    return `${fmt(DND_START_MIN)} – ${fmt(DND_END_MIN)}`
  }

  private assemble(): void {
    const services = appServices.services
    if (!services) return
    const now = Date.now()
    const dayStart = new Date()
    dayStart.setHours(0, 0, 0, 0)
    const cutoff = dayStart.getTime()
    const items: NotifItem[] = []

    // 消息类：未读会话
    for (const c of services.conversations.get().conversations) {
      if (c.unreadLocal > 0) {
        items.push({
          key: `m:${c.key}`,
          category: 'message',
          title: c.kind === 'group' ? c.title : c.peerId,
          body: c.lastMessagePreview ?? (c.kind === 'group' ? '新群组消息' : '发来消息'),
          at: c.lastMessageAt ?? now,
          unread: true,
        })
      }
    }

    // 好友请求类
    for (const r of services.friends.get().pendingIn) {
      items.push({
        key: `f:${r.requestId}`,
        category: 'friend',
        title: `${r.fromUserId} 请求加你为好友`,
        body: '前往处理',
        at: now,
        unread: true,
      })
    }

    // 组队邀请类
    for (const inv of services.partyState.get().invites) {
      items.push({
        key: `p:${inv.inviteId}`,
        category: 'party',
        title: `${inv.fromUserId} 邀请你组队`,
        body: '前往处理',
        at: now,
        unread: true,
      })
    }

    // 系统类：已注册推送设备
    for (const d of services.devices.get().devices) {
      items.push({
        key: `s:${d.deviceId}`,
        category: 'system',
        title: `推送已在 ${d.deviceName === '' ? d.platform : d.deviceName} 启用`,
        body: `${d.platform} · 注册于 ${this.fmtDate(d.registeredAt)}`,
        at: d.registeredAt > 0 ? d.registeredAt : now,
        unread: false,
      })
    }

    items.sort((a, b) => b.at - a.at)
    this.todayItems = items.filter((i) => i.at >= cutoff)
    this.earlierItems = items.filter((i) => i.at < cutoff)
    this.unreadTotal = items.filter((i) => i.unread).length
  }

  private fmtDate(ts: number): string {
    if (ts <= 0) return '—'
    const d = new Date(ts)
    const p = (n: number): string => (n < 10 ? `0${n}` : `${n}`)
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`
  }

  private markAllRead(): void {
    const services = appServices.services
    if (!services) return
    let changed = false
    for (const c of services.conversations.get().conversations) {
      if (c.unreadLocal > 0) {
        clearUnread(services.conversations, c.key)
        changed = true
      }
    }
    if (changed) {
      promptAction.showToast({ message: '已标记全部已读', duration: 1500 })
      this.assemble()
    }
  }

  aboutToAppear(): void {
    const services = appServices.services
    if (!services) {
      router.replaceUrl({ url: 'pages/HostPage' }).catch(() => {})
      return
    }
    this.refreshSettings()
    this.assemble()
    this.unsubsHolder = [
      services.conversations.subscribe(() => this.assemble()),
      services.friends.subscribe(() => this.assemble()),
      services.partyState.subscribe(() => this.assemble()),
      services.devices.subscribe(() => this.assemble()),
    ]
  }

  aboutToDisappear(): void {
    for (const off of this.unsubsHolder) off()
    this.unsubsHolder = []
  }

  private refreshSettings(): void {
    const s = appServices.notifySettings
    if (s) {
      this.systemPush = s.systemPush
      this.dnd = s.dnd
      this.mentionOnly = s.mentionOnly
    }
    void appServices.loadNotifySettings().then((loaded) => {
      this.systemPush = loaded.systemPush
      this.dnd = loaded.dnd
      this.mentionOnly = loaded.mentionOnly
    })
  }
}
