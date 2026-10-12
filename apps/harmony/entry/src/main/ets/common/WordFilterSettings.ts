import preferences from '@ohos.data.preferences';
import type common from '@ohos.app.ability.common';

/**
 * 聊天词库过滤开关（ProfilePage 设置卡行）：纯本地偏好，不新增协议能力。
 * 开 = 发送侧本地预检生效（WordFilterSync.onBeforeSend：REPLACE 就地改写、
 * REJECT 拦下），与 SDK 三端同契约；关 = 跳过本地预检（词库同步照常，重新
 * 打开即时生效——词库仍新鲜）。持久化走 @ohos.data.preferences，与
 * NotifySettings 同一工具链（每安装独立）。默认开：与
 * design/prototypes/mobile/05-profile.html 的 toggle on 一致。
 */
const PREFERENCES_NAME = 'chirp_wordfilter';
const KEY_ENABLED = 'enabled';

const DEFAULT_ENABLED = true;

let cached = DEFAULT_ENABLED;

/** 内存镜像：ChatApi 发送路径是同步判定，不能等 preferences 往返。 */
export function wordFilterEnabledCache(): boolean {
  return cached;
}

/** 页面写入与预载共用：更新内存镜像。 */
export function setWordFilterEnabledCache(enabled: boolean): void {
  cached = enabled;
}

export async function loadWordFilterSettings(context: common.UIAbilityContext): Promise<boolean> {
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  cached = (await store.get(KEY_ENABLED, DEFAULT_ENABLED)) as boolean;
  return cached;
}

export async function saveWordFilterEnabled(
  context: common.UIAbilityContext,
  enabled: boolean,
): Promise<void> {
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  await store.put(KEY_ENABLED, enabled);
  await store.flush();
  cached = enabled;
}
