/**
 * 唯一 UIAbility 入口（对齐 web 端单页壳 + 守卫路由的最小面）：
 * onCreate 组装 AppServices（五连接 + stores + APIs）并预载服务器地址；
 * 窗口内容固定为守卫页 pages/Index（aboutToAppear 按登录态分发）。
 */
import UIAbility from '@ohos.app.ability.UIAbility';
import AbilityConstant from '@ohos.app.ability.AbilityConstant';
import Want from '@ohos.app.ability.Want';
import window from '@ohos.window';
import { BusinessError } from '@ohos.base';
import { appServices } from '../common/AppServices';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    appServices.bindContext(this.context);
  }

  onDestroy(): void {
    // 进程级状态随实例释放；服务器地址与 device_id 已落盘，登录态不持久化
    // （stores 为内存镜像，重新登录即回拉，与 web 端刷新语义一致）。
    appServices.shutdown();
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err: BusinessError) => {
      // 加载失败无法恢复 UI（守卫页为最小静态布局）
    });
    appServices.prewarm().catch((_e: Error) => {
      // 预载失败由守卫页 resolve 时再行处理（此处仅提前预热）
    });
  }

  onWindowStageDestroy(): void {
    // 无常驻窗口资源
  }

  onForeground(): void {
    appServices.setForeground(true);
  }

  onBackground(): void {
    appServices.setForeground(false);
  }
}
