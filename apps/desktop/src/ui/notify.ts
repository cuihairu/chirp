import { isPermissionGranted, requestPermission, sendNotification } from '@tauri-apps/plugin-notification';
import { shouldNotifyFor } from '../api/desktop_notify';

/**
 * 桌面通知门面：Tauri 壳里走通知插件（webkit2gtk 的系统通知），纯浏览器
 * dev 环境回退 Web Notification API。两态都探测式降级，永不抛。
 */
const inTauri = (): boolean => typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

export async function ensureNotifyPermission(): Promise<boolean> {
  try {
    if (inTauri()) {
      if (await isPermissionGranted()) return true;
      const granted = await requestPermission();
      return granted === 'granted';
    }
    if (typeof Notification === 'undefined') return false;
    if (Notification.permission === 'granted') return true;
    if (Notification.permission === 'denied') return false;
    return (await Notification.requestPermission()) === 'granted';
  } catch {
    return false;
  }
}

export function fireNotify(title: string, body: string, tag: string, onClick: () => void): void {
  try {
    if (inTauri()) {
      sendNotification({ title, body });
      // Tauri 通知没有 onclick 回调（桌面端由系统通知中心接管）；窗口聚焦
      // 由用户点任务栏完成。tag 语义保留给未来按会话去重。
      void tag;
      return;
    }
    if (typeof Notification === 'undefined' || Notification.permission !== 'granted') return;
    const n = new Notification(title, { body, tag });
    n.onclick = () => {
      window.focus();
      onClick();
      n.close();
    };
  } catch {
    // 通知失败静默：消息本体仍在会话里。
  }
}

export { shouldNotifyFor };
