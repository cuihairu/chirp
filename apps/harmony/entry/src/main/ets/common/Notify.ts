import notification from '@ohos.notification';
import type common from '@ohos.app.ability.common';
import { shouldNotifyFor } from '../code/api/desktop_notify';

/**
 * 消息通知接线：权限申请一次（登录成功后用户可感的时机），通知在后台或
 * 非当前会话收到消息时弹出。判定面与 web 端同源（shouldNotifyFor）。
 */
let context: common.UIAbilityContext | null = null;
let permissionRequested = false;

export function bindNotifyContext(ctx: common.UIAbilityContext): void {
  context = ctx;
}

/** 登录成功后申请通知权限（用户拒绝则静默降级：仅应用内展示）。 */
export async function ensureNotificationPermission(): Promise<void> {
  if (permissionRequested || !context) return;
  permissionRequested = true;
  try {
    await notification.requestEnableNotification(context);
  } catch {
    // 拒绝/不支持：无系统通知，聊天不受影响。
  }
}

export async function showChatNotification(params: {
  title: string;
  content: string;
}): Promise<void> {
  if (!context) return;
  const request: notification.NotificationRequest = {
    id: nextId(),
    content: {
      notificationContentType: notification.ContentType.NOTIFICATION_CONTENT_BASIC_TEXT,
      normal: { title: params.title, text: params.content, additionalText: '' },
    },
  };
  try {
    await notification.publish(request);
  } catch {
    // 通知失败不追责：聊天面不受影响。
  }
}

/**
 * api.onMessage 的过滤回调：后台或非当前会话才弹（与 web 端一致——
 * 前台当前会话的消息上屏即可，不重复打扰）。
 */
export function notifyIfUnattended(params: {
  foreground: boolean;
  activeChannelKey: string | null;
  titleForChannel: string;
  fromUserId: string;
  content: string;
  channelKey: string;
}): void {
  const allowed = shouldNotifyFor(!params.foreground, params.activeChannelKey ?? undefined, {
    channel: { key: params.channelKey },
  });
  if (!allowed) return;
  void showChatNotification({ title: params.titleForChannel, content: `${params.fromUserId}: ${params.content}` });
}

let notifyId = 1000;
function nextId(): number {
  notifyId += 1;
  return notifyId;
}
