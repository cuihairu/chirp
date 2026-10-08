import router from '@ohos.router';
import { appServices } from '../common/AppServices';
import { C } from '../common/Tokens';

/**
 * 守卫页（唯一 launcher 入口，对齐 web 端 RequireAuth/LoginRoute 双向
 * 重定向）：prewarm 完成后按三态分发——未配置服务器 → HostPage，未登录 →
 * LoginPage，已登录 → SessionsPage。自身只是静态 logo 面，不承载业务。
 */
@Entry
@Component
struct Index {
  build() {
    Column({ space: 14 }) {
      Text('Chirp')
        .fontSize(40)
        .fontWeight(FontWeight.Bold)
        .fontColor(C.text)
      Text('Chirp 伴侣')
        .fontSize(15)
        .fontColor(C.text2)
      LoadingProgress()
        .width(28)
        .height(28)
        .color(C.accent)
        .margin({ top: 12 })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor(C.bg)
  }

  aboutToAppear(): void {
    appServices
      .ready()
      .then(() => this.dispatch())
      .catch(() => {
        // HostConfig 预载失败（存储异常）：按未配置处理，进 HostPage 重配。
        this.go('pages/HostPage');
      });
  }

  private dispatch(): void {
    if (!appServices.configured) {
      this.go('pages/HostPage');
      return;
    }
    if (!appServices.loggedInUser) {
      this.go('pages/LoginPage');
      return;
    }
    this.go('pages/SessionsPage');
  }

  private go(url: string): void {
    router.replaceUrl({ url }).catch(() => {
      // 路由失败无法恢复 UI（守卫页为最小静态布局）。
    });
  }
}
