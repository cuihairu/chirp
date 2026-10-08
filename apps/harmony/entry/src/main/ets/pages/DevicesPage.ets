import promptAction from '@ohos.promptAction';
import { appServices } from '../common/AppServices';
import { TabBar, TopBar } from '../common/widgets.ets';
import { C, R } from '../common/Tokens';
import { onlineDevicesOf } from '../code/state/online_devices_store';
import type { DeviceEntry } from '../code/state/device_store';
import type { OnlineDeviceView } from '../code/state/online_devices_store';

/**
 * 设备页（tab）：推送目标管理（本设备注册态/其他端移除）+ 多端在线
 * （P0）+ 游戏在线状态开关（P0）。device 平面不可用时相应段降级说明。
 */
@Entry
@Component
struct DevicesPage {
  @State devices: DeviceEntry[] = [];
  @State selfRegistered: boolean = false;
  @State deviceUnavailable: boolean = true;
  @State online: OnlineDeviceView[] = [];
  @State gameEnabled: boolean = true;
  @State gameUnavailable: boolean = true;
  @State gameIds: string[] = [];
  private selfDeviceId: string = '';
  private unsubsHolder: Array<() => void> = [];

  build() {
    Column() {
      TopBar({ title: '设备' })
      Scroll() {
        Column() {
          if (!this.deviceUnavailable) {
            this.SectionHeader('设备管理')
            this.DeviceCard()
          } else {
            this.Note('设备服务未连接')
          }
          this.SectionHeader('在线设备')
          this.OnlineCard()
          if (!this.gameUnavailable) {
            this.SectionHeader('游戏在线状态')
            this.GameCard()
          }
        }
        .width('100%')
        .padding({ left: 16, right: 16, bottom: 16 })
      }
      .layoutWeight(1)
      .scrollBar(BarState.Off)
      .align(Alignment.Top)
      TabBar({ active: 'devices' })
    }
    .width('100%')
    .height('100%')
    .backgroundColor(C.bg)
  }

  @Builder
  private SectionHeader(label: string) {
    Text(label)
      .fontSize(12)
      .fontColor(C.text3)
      .width('100%')
      .margin({ top: 14, bottom: 6 })
  }

  @Builder
  private Note(text: string) {
    Text(text)
      .fontSize(13)
      .fontColor(C.text3)
      .width('100%')
      .padding(12)
  }

  @Builder
  private DeviceCard() {
    Column({ space: 8 }) {
      Text('本设备:' + (this.selfRegistered ? '已注册' : '本设备尚未注册为推送目标。'))
        .fontSize(13)
        .fontColor(this.selfRegistered ? C.text2 : C.warn)
      Text('离线消息会推送到这里列出的设备(当前为日志传输,真实厂商推送待接入)。')
        .fontSize(11)
        .fontColor(C.text3)
      if (this.devices.length === 0) {
        Text('没有任何已注册设备。')
          .fontSize(13)
          .fontColor(C.text3)
          .padding(8)
      }
      ForEach(this.devices, (d: DeviceEntry) => {
        Row({ space: 10 }) {
          Column({ space: 2 }) {
            Row({ space: 6 }) {
              Text(d.deviceName === '' ? d.deviceId : d.deviceName)
                .fontSize(14)
                .fontColor(C.text)
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .layoutWeight(1)
              if (d.deviceId === this.selfDeviceId) {
                Text('本设备')
                  .fontSize(10)
                  .fontColor(C.accentSoft)
                  .padding({ left: 6, right: 6, top: 1, bottom: 1 })
                  .borderRadius(R.pill)
                  .backgroundColor(C.accentDim)
              }
            }
            .width('100%')
            Text(`${d.platform} · ${d.osVersion} · 注册于 ${this.fmtDate(d.registeredAt)}`)
              .fontSize(11)
              .fontColor(C.text3)
              .maxLines(1)
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          if (d.deviceId !== this.selfDeviceId) {
            Text('移除')
              .fontSize(12)
              .fontColor(C.danger)
              .padding(6)
              .onClick(() => this.confirmUnregister(d))
          }
        }
        .width('100%')
        .padding(10)
        .backgroundColor(C.surface2)
        .borderRadius(R.row)
      }, (d: DeviceEntry) => d.deviceId)
    }
    .width('100%')
    .padding(14)
    .backgroundColor(C.surface)
    .borderRadius(R.card)
  }

  @Builder
  private OnlineCard() {
    Column({ space: 8 }) {
      Text('同一账号在其他类型端(web/ios/android…)上的在线状态;同类型端新登录会顶掉旧会话。')
        .fontSize(11)
        .fontColor(C.text3)
      if (this.online.length === 0) {
        Text('当前没有其他在线端。')
          .fontSize(13)
          .fontColor(C.text3)
          .padding(8)
      }
      ForEach(this.online, (d: OnlineDeviceView) => {
        Row({ space: 10 }) {
          Circle()
            .width(8)
            .height(8)
            .fill(d.online ? C.online : C.text3)
          Text(d.platform)
            .fontSize(14)
            .fontColor(C.text)
            .layoutWeight(1)
          Text(d.online ? '在线' : '离线')
            .fontSize(12)
            .fontColor(d.online ? C.online : C.text3)
        }
        .width('100%')
        .padding(10)
        .backgroundColor(C.surface2)
        .borderRadius(R.row)
      }, (d: OnlineDeviceView) => `${d.platform}:${d.online}:${d.at}`)
    }
    .width('100%')
    .padding(14)
    .backgroundColor(C.surface)
    .borderRadius(R.card)
  }

  @Builder
  private GameCard() {
    Column({ space: 10 }) {
      Row({ space: 10 }) {
        Column({ space: 2 }) {
          Text('游戏在线状态')
            .fontSize(14)
            .fontColor(C.text)
          Text('开启后好友可见你正在玩的游戏,好友私聊消息也会同步投递进游戏内;关闭后两者都停止。')
            .fontSize(11)
            .fontColor(C.text3)
        }
        .alignItems(HorizontalAlign.Start)
        .layoutWeight(1)
        Toggle({ type: ToggleType.Switch, isOn: this.gameEnabled })
          .selectedColor(C.accent)
          .onChange((on: boolean) => void this.setGameEnabled(on))
      }
      .width('100%')
      Text(this.gameEnabled
        ? (this.gameIds.length > 0 ? `当前生效:${this.gameIds.join('、')}` : '尚未绑定游戏,绑定后自动开始上报在线状态。')
        : '已关闭:好友看不到你的游戏状态,消息也不进游戏。')
        .fontSize(12)
        .fontColor(this.gameEnabled ? C.text2 : C.text3)
    }
    .width('100%')
    .padding(14)
    .backgroundColor(C.surface)
    .borderRadius(R.card)
  }

  aboutToAppear(): void {
    const services = appServices.services;
    if (!services) return;
    this.selfDeviceId = services.auth.get().deviceId;
    this.refreshLocal();
    this.unsubsHolder = [
      services.devices.subscribe(() => this.refreshDeviceLocal()),
      services.onlineDevices.subscribe(() => this.refreshOnlineLocal()),
      services.gamePresence.subscribe(() => this.refreshGameLocal()),
    ];
    void services.deviceApi?.refreshDevices();
    void services.gamePresenceApi?.refresh();
  }

  aboutToDisappear(): void {
    for (const off of this.unsubsHolder) off();
    this.unsubsHolder = [];
  }

  private refreshLocal(): void {
    this.refreshDeviceLocal();
    this.refreshOnlineLocal();
    this.refreshGameLocal();
  }

  private refreshDeviceLocal(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.devices.get();
    this.devices = [...state.devices];
    this.selfRegistered = state.selfRegistered;
    this.deviceUnavailable = state.unavailable;
  }

  private refreshOnlineLocal(): void {
    const services = appServices.services;
    if (!services) return;
    this.online = onlineDevicesOf(services.onlineDevices.get());
  }

  private refreshGameLocal(): void {
    const services = appServices.services;
    if (!services) return;
    const state = services.gamePresence.get();
    this.gameEnabled = state.enabled;
    this.gameUnavailable = state.unavailable;
    this.gameIds = [...state.games];
  }

  private fmtDate(ts: number): string {
    if (ts <= 0) return '—';
    const d = new Date(ts);
    const p = (n: number): string => (n < 10 ? `0${n}` : `${n}`);
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
  }

  private confirmUnregister(d: DeviceEntry): void {
    const name = d.deviceName === '' ? d.deviceId : d.deviceName;
    promptAction.showActionMenu({
      buttons: [{ text: `移除推送目标 ${name}?`, color: C.danger }],
    }).then((res: promptAction.ActionMenuSuccessResponse) => {
      if (res.index !== 0) return;
      const services = appServices.services;
      void services?.deviceApi?.unregister(d.deviceId);
    }).catch(() => {});
  }

  private async setGameEnabled(on: boolean): Promise<void> {
    const services = appServices.services;
    if (!services || !services.gamePresenceApi) return;
    const ok = await services.gamePresenceApi.setEnabled(on);
    if (!ok) {
      this.refreshGameLocal();
      promptAction.showToast({ message: '设置未保存,请稍后重试。', duration: 2000 });
    }
  }
}
