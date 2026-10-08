import router from '@ohos.router';
import { errorText } from '@chirp/app-protocol/errors';
import { appServices } from '../common/AppServices';
import { C, R } from '../common/Tokens';

/**
 * 登录页（对齐 web LoginPage 的一期形态）：用户 ID 即 token（dev scaffold
 * 模式）。主聊天登录成功才进入会话页；四个附属平面由 AppServices 并行
 * best-effort 接线，失败各自降级。
 */
@Entry
@Component
struct LoginPage {
  @State userId: string = '';
  @State busy: boolean = false;
  @State error: string = '';

  build() {
    Column() {
      Column({ space: 8 }) {
        Text('登录到 Chirp')
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
          .fontColor(C.text)
        Text('开发模式:输入用户 ID 即可登录')
          .fontSize(13)
          .fontColor(C.text2)
      }
      .alignItems(HorizontalAlign.Start)
      .width('100%')
      .margin({ bottom: 24 })

      TextInput({ placeholder: '用户 ID', text: this.userId })
        .type(InputType.Normal)
        .fontSize(15)
        .fontColor(C.text)
        .placeholderColor(C.text3)
        .backgroundColor(C.surface2)
        .borderRadius(R.row)
        .height(48)
        .width('100%')
        .enabled(!this.busy)
        .onChange((value: string) => {
          this.userId = value;
          this.error = '';
        })

      if (this.error !== '') {
        Text(this.error)
          .fontSize(13)
          .fontColor(C.danger)
          .margin({ top: 10 })
      }

      Button(this.busy ? '登录中…' : '登录')
        .fontSize(16)
        .fontColor(C.text)
        .backgroundColor(C.accent)
        .borderRadius(999)
        .height(44)
        .width('100%')
        .margin({ top: 20 })
        .enabled(!this.busy && this.userId.trim() !== '')
        .opacity(this.userId.trim() === '' ? 0.6 : 1)
        .onClick(() => void this.onSubmit())
    }
    .padding({ left: 24, right: 24 })
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor(C.bg)
  }

  private async onSubmit(): Promise<void> {
    const id = this.userId.trim();
    if (this.busy || id === '') return;
    this.busy = true;
    this.error = '';
    try {
      const code = await appServices.loginAll(id);
      if (code === 0) {
        await router.replaceUrl({ url: 'pages/SessionsPage' });
      } else {
        this.error = errorText(code);
      }
    } catch {
      this.error = '无法连接到服务器,请稍后再试';
    } finally {
      this.busy = false;
    }
  }
}
