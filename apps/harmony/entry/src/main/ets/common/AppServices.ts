import type common from '@ohos.app.ability.common';
import { PresenceStatus } from '@chirp/proto/social';
import { planeUrls, createServices, type Services } from '../code/api/services';
import type { WebSocketLike } from '@chirp/app-protocol/chirp_client';
import { HarmonyWebSocket } from './HarmonyWebSocket';
import {
  cachedHostConfig,
  loadHostConfig,
  saveBaseUrl,
  type HostConfigData,
} from './HostConfig';
import {
  bindNotifyContext,
  ensureNotificationPermission,
  notifyIfUnattended,
} from './Notify';

/**
 * 应用级单例（EntryAbility 与各页面共用的服务面）：组装五连接 + stores +
 * APIs，管理服务器地址、登录编排与生命周期。EntryAbility 契约：
 * bindContext / prewarm / setForeground / shutdown；页面契约：
 * configure / loginAll / logoutAll / services / configured。
 */
class AppServices {
  private context: common.UIAbilityContext | null = null;
  private servicesRef: Services | null = null;
  private prewarmPromise: Promise<HostConfigData> | null = null;
  private foreground = true;
  private unsubs: Array<() => void> = [];

  /** EntryAbility.onCreate：先绑上下文（preferences/deviceInfo 需要）。 */
  bindContext(context: common.UIAbilityContext): void {
    this.context = context;
    bindNotifyContext(context);
  }

  /** 预载 HostConfig（幂等）：deviceId 落盘、deviceName 归纳。 */
  prewarm(): Promise<HostConfigData> {
    if (!this.prewarmPromise) {
      this.prewarmPromise = loadHostConfig(this.requireContext());
    }
    return this.prewarmPromise;
  }

  get configured(): boolean {
    return (cachedHostConfig()?.baseUrl ?? '') !== '' || this.servicesRef !== null;
  }

  get services(): Services | null {
    return this.servicesRef;
  }

  get isForeground(): boolean {
    return this.foreground;
  }

  /** 守卫页等 ready 语义：prewarm 完成即可判定登录态与配置态。 */
  ready(): Promise<HostConfigData> {
    return this.prewarm();
  }

  get loggedInUser(): string | null {
    return this.servicesRef?.auth.get().userId ?? null;
  }

  /** 当前打开的会话（通知过滤与未读豁免共用）。 */
  private activeChannelKey: string | null = null;

  setActiveChannel(key: string | null): void {
    this.activeChannelKey = key;
  }

  get activeChannel(): string | null {
    return this.activeChannelKey;
  }

  setForeground(value: boolean): void {
    this.foreground = value;
  }

  /** HostPage：保存并按新地址重建服务面（旧连接先断）。 */
  async configure(baseUrl: string): Promise<void> {
    const ctx = this.requireContext();
    await saveBaseUrl(ctx, baseUrl);
    this.teardownServices();
    const config = await this.prewarm();
    this.servicesRef = createServices({
      wsFactory: wsFactory,
      deviceId: config.deviceId,
      deviceName: config.deviceName,
      ...planeUrls(config.baseUrl),
    });
  }

  /**
   * LoginPage：主聊天登录（失败回错误码），成功后四个附属平面并行
   * best-effort 登录（任一挂了聊天照常，特性面各自隐藏），并申请通知
   * 权限 + 接线消息通知。
   */
  async loginAll(userId: string): Promise<number> {
    const services = this.servicesRef;
    if (!services) {
      // 理论不可达（守卫页保证先 configure）；按内部错误返回。
      return 1;
    }
    const code = await services.api.login(userId);
    if (code !== 0) return code;
    this.wireNotifications();
    void ensureNotificationPermission();

    const planeLogins: Array<Promise<void>> = [];
    if (services.socialApi) {
      planeLogins.push(
        services.socialApi
          .login(userId)
          .then((ok) => {
            if (ok) void services.socialApi?.setPresence(PresenceStatus.ONLINE);
          })
          .catch(() => {}),
      );
    }
    if (services.partyApi) planeLogins.push(services.partyApi.login(userId).catch(() => {}));
    if (services.voiceApi) planeLogins.push(services.voiceApi.login(userId).catch(() => {}));
    if (services.deviceApi) planeLogins.push(services.deviceApi.login(userId).catch(() => {}));
    if (services.gamePresenceApi) {
      planeLogins.push(services.gamePresenceApi.onLoggedIn().catch(() => {}));
    }
    void Promise.all(planeLogins).catch(() => {});
    return 0;
  }

  /** 全平面登出（对齐 web 端 signOut）。 */
  async logoutAll(): Promise<void> {
    const services = this.servicesRef;
    if (!services) return;
    services.socialApi?.logout();
    services.partyApi?.logout();
    services.voiceApi?.logout();
    services.gamePresenceApi?.logout();
    // 设备面登出只是断连 + 面板隐藏，失败不阻断主流程。
    try {
      services.deviceApi?.logout();
    } catch {
      // ignore
    }
    await services.api.logout();
    this.detachNotifications();
  }

  /** EntryAbility.onDestroy：断开全部连接。 */
  shutdown(): void {
    this.detachNotifications();
    this.teardownServices();
    this.prewarmPromise = null;
  }

  private teardownServices(): void {
    const services = this.servicesRef;
    if (!services) return;
    this.detachNotifications();
    services.socialApi?.logout();
    services.partyApi?.logout();
    services.voiceApi?.logout();
    services.api.stop();
    services.client.disconnect();
    services.social?.disconnect();
    services.party?.disconnect();
    services.voice?.disconnect();
    services.device?.disconnect();
    this.servicesRef = null;
  }

  private wireNotifications(): void {
    const services = this.servicesRef;
    if (!services) return;
    this.detachNotifications();
    this.unsubs = [
      services.api.onMessage((message) => {
        const title =
          message.channel.kind === 'group'
            ? `${conversationTitle(message.channel.key)} · ${message.fromUserId}`
            : message.fromUserId;
        notifyIfUnattended({
          foreground: this.foreground,
          activeChannelKey: this.activeChannelKey,
          titleForChannel: title,
          fromUserId: message.fromUserId,
          content: message.content,
          channelKey: message.channel.key,
        });
      }),
    ];
  }

  private detachNotifications(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  private requireContext(): common.UIAbilityContext {
    if (!this.context) {
      // EntryAbility.onCreate 先于一切页面生命周期，缺绑定为编程错误。
      throw new Error('AppServices: context not bound');
    }
    return this.context;
  }
}

function conversationTitle(channelKey: string): string {
  const conversations = appServices.servicesRef?.conversations.get().conversations ?? [];
  const found = conversations.find((c) => c.key === channelKey);
  if (found) return found.title;
  return channelKey.startsWith('g:') ? channelKey.slice(2) : channelKey;
}

/** 鸿蒙 WebSocket 工厂：createServices 的必传注入点。 */
const wsFactory = (url: string): WebSocketLike => new HarmonyWebSocket(url);

export const appServices = new AppServices();
