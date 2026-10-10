import router from '@ohos.router';
import promptAction from '@ohos.promptAction';
import pasteboard from '@ohos.pasteboard';
import { appServices } from '../common/AppServices';
import { TopBar, Avatar, PillButton } from '../common/widgets.ets';
import { C, R, S } from '../common/Tokens';
import { onlineDevicesOf } from '../code/state/online_devices_store';
import type { OnlineDeviceView } from '../code/state/online_devices_store';
import type { DeviceEntry } from '../code/state/device_store';

/**
 * 我的页（参照 design/prototypes/mobile/05-profile.html）：
 * 个人卡 + 在线设备卡 + 设置列表 + 退出登录。
 * 入口：会话列表顶栏头像（不占五页签）。
 */
interface DeviceRow {
  key: string;
  platform: string;
  name: string;
  online: boolean;
  self: boolean;
  pushEnabled: boolean;
}

@Entry
@Component
struct ProfilePage {
  @State userId: string = '';
  @State online: DeviceRow[] = [];
  @State unreadCount: number = 0;
  private unsubsHolder: Array<() => void> = [];

  build() {
    Column() {
      TopBar({ title: '我的' })
      Scroll() {
        Column() {
          this.ProfileCard()
          this.OnlineCard()
          this.SettingsCard()
          PillButton({
            label: '退出登录',
            destructive: true,
            onTap: () => void this.onLogout(),
          })
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

  /** 个人卡：大头像 + 在线点 + 用户名 + ID + 复制。 */
  @Builder
  private ProfileCard() {
    Row({ space: 16 }) {
      Stack({ alignContent: Alignment.BottomEnd }) {
        Avatar({ name: this.userId, size: S.avatarLg })
        if (this.isSelfOnline()) {
          Circle()
            .width(14)
            .height(14)
            .fill(C.online)
            .stroke(C.bg)
            .strokeWidth(2)
        }
      }

      Column({ space: 4 }) {
        Text(this.userId)
          .fontSize(19)
          .fontWeight(FontWeight.Bold)
          .fontColor(C.text)
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
        Row({ space: 4 }) {
          Text(`ID: ${this.userId}`)
            .fontSize(13)
            .fontColor(C.text2)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
          Text('复制')
            .fontSize(13)
            .fontColor(C.accentSoft)
            .padding({ left: 6, right: 6, top: 2, bottom: 2 })
            .borderRadius(R.pill)
            .backgroundColor(C.accentDim)
            .onClick(() => void this.copyId())
        }
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 20, bottom: 20 })
    .backgroundColor(C.surface)
    .borderRadius(R.card)
  }

  /** 在线设备卡：本机 + 其他端在线态 + 顶号警示。 */
  @Builder
  private OnlineCard() {
    Column({ space: 8 }) {
      Row() {
        Text('在线设备')
          .fontSize(12)
          .fontColor(C.text3)
        Text(`(${this.online.length})`)
          .fontSize(12)
          .fontColor(C.text3)
      }
      .width('100%')
      .margin({ top: 14, bottom: 6 })

      Column({ space: 10 }) {
        ForEach(this.online, (d: DeviceRow) => {
          this.DeviceRow(d)
        }, (d: DeviceRow) => d.key)
      }
      .width('100%')
      .padding(14)
      .backgroundColor(C.surface)
      .borderRadius(R.card)

      Row({ space: 8 }) {
        Text('同账号在他端登录会顶掉当前会话——踢线是终态，SDK 停止重连并弹回登录页')
          .fontSize(12)
          .fontColor(C.text3)
      }
      .width('100%')
      .padding({ left: 14, right: 14, top: 10, bottom: 10 })
      .backgroundColor(C.surface2)
      .borderRadius(R.row)
    }
  }

  @Builder
  private DeviceRow(d: DeviceRow) {
    Row({ space: 10 }) {
      Row() {
        Text(d.self ? '📱' : '🖥')
          .fontSize(18)
      }
      .width(34)
      .height(34)
      .justifyContent(FlexAlign.Center)
      .alignItems(VerticalAlign.Center)
      .backgroundColor(C.surface2)
      .borderRadius(8)

      Column({ space: 2 }) {
        Row({ space: 6 }) {
          Text(d.name)
            .fontSize(14)
            .fontColor(C.text)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
          if (d.self) {
            Text('本机')
              .fontSize(10)
              .fontColor(C.accentSoft)
              .padding({ left: 6, right: 6, top: 1, bottom: 1 })
              .borderRadius(R.pill)
              .backgroundColor(C.accentDim)
          }
        }
        Text(`${d.platform}${d.pushEnabled ? ' · 推送已启用' : d.online ? ' · 在线' : ' · 离线'}`)
          .fontSize(12)
          .fontColor(C.text3)
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)

      if (d.online || d.self) {
        Circle()
          .width(8)
          .height(8)
          .fill(C.online)
      } else {
        Circle()
          .width(8)
          .height(8)
          .fill(C.text3)
      }
    }
    .width('100%')
  }

  /** 设置列表：通知设置 / 深色模式 / 关于 Chirp。 */
  @Builder
  private SettingsCard() {
    Column({ space: 8 }) {
      Row() {
        Text('设置')
          .fontSize(12)
          .fontColor(C.text3)
          .layoutWeight(1)
      }
      .width('100%')
      .margin({ top: 14, bottom: 6 })

      Column({ space: 0 }) {
        // 通知设置 → 跳通知中心
        Row({ space: 10 }) {
          Text('通知设置').fontSize(14).fontColor(C.text).layoutWeight(1)
          if (this.unreadCount > 0) {
            Text(`${this.unreadCount} 条未读`)
              .fontSize(11)
              .fontColor(C.text2)
              .padding({ left: 8, right: 8, top: 2, bottom: 2 })
              .borderRadius(R.pill)
              .backgroundColor(C.surface2)
          }
          Text('›').fontSize(20).fontColor(C.text3)
        }
        .width('100%')
        .padding({ top: 12, bottom: 12 })
        .onClick(() => router.pushUrl({ url: 'pages/NotificationsPage' }).catch(() => {}))

        // 深色模式（固定深色 token 体系，无浅色变体）
        Row({ space: 10 }) {
          Text('深色模式').fontSize(14).fontColor(C.text).layoutWeight(1)
          Text('深色')
            .fontSize(11)
            .fontColor(C.text2)
            .padding({ left: 8, right: 8, top: 2, bottom: 2 })
            .borderRadius(R.pill)
            .backgroundColor(C.surface2)
        }
        .width('100%')
        .padding({ top: 12, bottom: 12 })

        // 关于 Chirp
        Row({ space: 10 }) {
          Text('关于 Chirp').fontSize(14).fontColor(C.text).layoutWeight(1)
          Text('v0.1.0')
            .fontSize(11)
            .fontColor(C.text2)
            .padding({ left: 8, right: 8, top: 2, bottom: 2 })
            .borderRadius(R.pill)
            .backgroundColor(C.surface2)
          Text('›').fontSize(20).fontColor(C.text3)
        }
        .width('100%')
        .padding({ top: 12, bottom: 12 })
        .onClick(() => promptAction.showToast({ message: 'Chirp 伴侣 v0.1.0 · 鸿蒙版', duration: 2000 }))
      }
      .width('100%')
      .padding(14)
      .backgroundColor(C.surface)
      .borderRadius(R.card)
    }
  }

  private isSelfOnline(): boolean {
    const services = appServices.services
    if (!services) return false
    return services.client.status === 'connected'
  }

  private refresh(): void {
    const services = appServices.services
    if (!services) return
    this.userId = services.auth.get().userId ?? ''

    // 未读角标取会话未读总和（与通知中心同源）。
    const convs = services.conversations.get().conversations
    this.unreadCount = convs.reduce((sum, c) => sum + c.unreadLocal, 0)

    // 构建在线设备行：本机（取 device_store） + 其他端（online_devices_store）。
    const selfId = services.auth.get().deviceId
    const rows: DeviceRow[] = []
    const selfDevice = services.devices.get().devices.find((d: DeviceEntry) => d.deviceId === selfId)
    if (selfDevice) {
      rows.push({
        key: 'self',
        platform: selfDevice.platform,
        name: selfDevice.deviceName === '' ? selfDevice.platform : selfDevice.deviceName,
        online: true,
        self: true,
        pushEnabled: services.devices.get().selfRegistered,
      })
    } else if (!services.devices.get().unavailable) {
      rows.push({
        key: 'self',
        platform: 'harmony',
        name: '本机',
        online: true,
        self: true,
        pushEnabled: false,
      })
    }
    for (const other of onlineDevicesOf(services.onlineDevices.get())) {
      rows.push({
        key: other.platform,
        platform: other.platform,
        name: other.platform,
        online: other.online,
        self: false,
        pushEnabled: false,
      })
    }
    this.online = rows
  }

  private async copyId(): Promise<void> {
    if (this.userId === '') return
    try {
      const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, this.userId)
      await pasteboard.getSystemPasteboard().setData(data)
      promptAction.showToast({ message: `已复制 ID`, duration: 1500 })
    } catch {
      promptAction.showToast({ message: '复制失败', duration: 1500 })
    }
  }

  private onLogout(): void {
    promptAction.showActionMenu({
      buttons: [
        { text: '取消', color: C.text3 },
        { text: '退出登录', color: C.danger },
      ],
    })
      .then((res: promptAction.ActionMenuSuccessResponse) => {
        if (res.index !== 1) return
        void appServices.logoutAll()
        void router.replaceUrl({ url: 'pages/LoginPage' })
      })
      .catch(() => {})
  }

  aboutToAppear(): void {
    const services = appServices.services
    if (!services) {
      router.replaceUrl({ url: 'pages/HostPage' }).catch(() => {})
      return
    }
    this.refresh()
    this.unsubsHolder = [
      services.conversations.subscribe(() => this.refresh()),
      services.devices.subscribe(() => this.refresh()),
      services.onlineDevices.subscribe(() => this.refresh()),
      services.client.onStatus(() => this.refresh()),
    ]
  }

  aboutToDisappear(): void {
    for (const off of this.unsubsHolder) off()
    this.unsubsHolder = []
  }
}
