import preferences from '@ohos.data.preferences';
import type common from '@ohos.app.ability.common';

/**
 * 推送设置（通知中心「推送设置」卡的三个开关）：三项都是纯本地偏好，不新增
 * 任何协议能力。判定面与消息通知抑制同源——Notify.ts 在 shouldNotifyFor
 * （web 端同款判定）通过之后再看这三项，故判定逻辑独立成纯函数便于核对。
 * 持久化走 @ohos.data.preferences，与 HostConfig 同一工具链（每安装独立）。
 * 默认值与 design/prototypes/mobile/04-notifications.html 一致：系统推送开、
 * 免打扰开（23:30–08:00）、仅好友与群提及关。
 */
const PREFERENCES_NAME = 'chirp_notify';
const KEY_SYSTEM_PUSH = 'system_push';
const KEY_DND = 'dnd';
const KEY_MENTION_ONLY = 'mention_only';

/** 免打扰窗口（一天的绝对分钟数）：起 > 止 视为跨零点的夜间段。 */
export const DND_START_MIN = 23 * 60 + 30;
export const DND_END_MIN = 8 * 60;

export interface NotifySettings {
  /** 系统通知总开关（落地为 @ohos.notification 的 enable/disable）。 */
  systemPush: boolean;
  /** 免打扰时段内静默。 */
  dnd: boolean;
  /** 仅好友私聊与群 @提及时通知（私聊始终通知）。 */
  mentionOnly: boolean;
}

export const DEFAULT_NOTIFY_SETTINGS: NotifySettings = {
  systemPush: true,
  dnd: true,
  mentionOnly: false,
};

let cached: NotifySettings = { ...DEFAULT_NOTIFY_SETTINGS };

/** 内存镜像：通知回调是同步路径，不能等 preferences 往返。 */
export function notifySettingsCache(): NotifySettings {
  return cached;
}

/** 页面写入与预载共用：更新内存镜像。 */
export function setNotifySettingsCache(settings: NotifySettings): void {
  cached = settings;
}

export async function loadNotifySettings(context: common.UIAbilityContext): Promise<NotifySettings> {
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  const next: NotifySettings = {
    systemPush: (await store.get(KEY_SYSTEM_PUSH, DEFAULT_NOTIFY_SETTINGS.systemPush)) as boolean,
    dnd: (await store.get(KEY_DND, DEFAULT_NOTIFY_SETTINGS.dnd)) as boolean,
    mentionOnly: (await store.get(KEY_MENTION_ONLY, DEFAULT_NOTIFY_SETTINGS.mentionOnly)) as boolean,
  };
  cached = next;
  return next;
}

export async function saveNotifySettings(
  context: common.UIAbilityContext,
  settings: NotifySettings,
): Promise<void> {
  const store = await preferences.getPreferences(context, PREFERENCES_NAME);
  await store.put(KEY_SYSTEM_PUSH, settings.systemPush);
  await store.put(KEY_DND, settings.dnd);
  await store.put(KEY_MENTION_ONLY, settings.mentionOnly);
  await store.flush();
  cached = settings;
}

/** 是否落在免打扰窗口内（跨零点段：起 > 止 时夜间为两段）。 */
export function inDndRange(settings: NotifySettings, date = new Date()): boolean {
  if (!settings.dnd) return false;
  const minutes = date.getHours() * 60 + date.getMinutes();
  if (DND_START_MIN > DND_END_MIN) {
    return minutes >= DND_START_MIN || minutes < DND_END_MIN;
  }
  return minutes >= DND_START_MIN && minutes < DND_END_MIN;
}

/** @提及判定：`@userId` 后须是非标识符字符或结尾，避免 @bob 命中 @bob2。 */
export function isMentioned(content: string, userId: string): boolean {
  if (userId === '') return false;
  const needle = `@${userId}`;
  let idx = content.indexOf(needle);
  while (idx !== -1) {
    const ch = content.charAt(idx + needle.length);
    if (ch === '' || !/[A-Za-z0-9_]/.test(ch)) return true;
    idx = content.indexOf(needle, idx + needle.length);
  }
  return false;
}

/**
 * 本地偏好对通知的抑制判定（shouldNotifyFor 之后的第二道）：总开关关、
 * 免打扰时段内、或仅提及且群消息未 @本人。纯函数，便于真机验证时逐条核对。
 */
export function settingsSuppress(
  settings: NotifySettings,
  params: { groupChannel: boolean; content: string; selfUserId: string },
): boolean {
  if (!settings.systemPush) return true;
  if (inDndRange(settings)) return true;
  if (settings.mentionOnly && params.groupChannel && !isMentioned(params.content, params.selfUserId)) {
    return true;
  }
  return false;
}
