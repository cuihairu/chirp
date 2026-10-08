import router from '@ohos.router';
import { appServices } from '../common/AppServices';
import { C, R } from '../common/Tokens';
import { isValidBaseUrl } from '../common/HostConfig';

/**
 * 服务器配置页（web 端无此页——浏览器有同源默认地址，设备上没有）：
 * 输入反代基址（如 ws://192.168.1.5:8080），按五平面约定拼 /ws/*。
 * 保存后重建服务面并进入登录页。
 */
@Entry
@Component
struct HostPage {
  @State baseUrl: string = '';
  @State busy: boolean = false;
  @State error: string = '';

  build() {
    Column() {
      Column({ space: 8 }) {
        Text('连接到服务器')
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
          .fontColor(C.text)
        Text('输入 Chirp 服务器的 WebSocket 基址；聊天/好友/组队/语音/设备五个平面按 base + /ws/{chat,social,party,voice,device} 拼出。')
          .fontSize(13)
          .fontColor(C.text2)
          .lineHeight(19)
      }
      .alignItems(HorizontalAlign.Start)
      .width('100%')
      .margin({ bottom: 24 })

      TextInput({ placeholder: 'ws://192.168.1.5:8080', text: this.baseUrl })
        .type(InputType.Normal)
        .fontSize(15)
        .fontColor(C.text)
        .placeholderColor(C.text3)
        .backgroundColor(C.surface2)
        .borderRadius(R.row)
        .height(48)
        .width('100%')
        .onChange((value: string) => {
          this.baseUrl = value;
          this.error = '';
        })

      if (this.error !== '') {
        Text(this.error)
          .fontSize(13)
          .fontColor(C.danger)
          .margin({ top: 10 })
      }

      Button(this.busy ? '连接中…' : '连接')
        .fontSize(16)
        .fontColor(C.text)
        .backgroundColor(C.accent)
        .borderRadius(999)
        .height(44)
        .width('100%')
        .margin({ top: 20 })
        .enabled(!this.busy && this.baseUrl.trim() !== '')
        .opacity(this.baseUrl.trim() === '' ? 0.6 : 1)
        .onClick(() => void this.onConnect())

      if (this.baseUrl.trim() !== '' && !isValidBaseUrl(this.baseUrl)) {
        Text('地址需以 ws:// 或 wss:// 开头')
          .fontSize(13)
          .fontColor(C.warn)
          .margin({ top: 10 })
      }
    }
    .padding({ left: 24, right: 24 })
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor(C.bg)
  }

  private async onConnect(): Promise<void> {
    const trimmed = this.baseUrl.trim();
    if (this.busy || trimmed === '') return;
    if (!isValidBaseUrl(trimmed)) {
      this.error = '地址需以 ws:// 或 wss:// 开头';
      return;
    }
    this.busy = true;
    this.error = '';
    try {
      await appServices.configure(trimmed);
      await router.replaceUrl({ url: 'pages/LoginPage' });
    } catch {
      this.error = '保存失败,请稍后再试';
    } finally {
      this.busy = false;
    }
  }
}
