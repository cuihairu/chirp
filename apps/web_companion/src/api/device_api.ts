import { DeviceInfo } from '@chirp/proto/app_notification';
import { GET_USER_DEVICES, REGISTER_DEVICE, LOGIN, UNREGISTER_DEVICE } from '../protocol/msg_map';
import {
  setDevices,
  setSelfRegistered,
  setUnavailable,
  type DeviceEntry,
  type DeviceState,
} from '../state/device_store';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import type { ChatConnection } from './chat_api';
import { uaSummary } from './ua_summary';

export interface DeviceApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  devices: Store<DeviceState>;
}

/**
 * Device plane api (app_gateway WS 5201): registers this browser as a push
 * target and lists/unregisters the account's devices. Degradeable like the
 * social and party planes — when the edge is down chat keeps working and the
 * devices entry hides. The server pins user_id on every forwarded request,
 * so nothing here can address another account.
 */
export class DeviceApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly devices: Store<DeviceState>;

  constructor(deps: DeviceApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.devices = deps.devices;
  }

  async login(userId: string): Promise<boolean> {
    if (this.conn.status !== 'connected') {
      await this.conn.connect();
    }
    const resp = await this.conn.request(LOGIN, {
      token: userId,
      deviceId: this.auth.get().deviceId,
      platform: 'web',
    });
    if (resp.code !== 0) {
      this.conn.disconnect();
      setUnavailable(this.devices, true);
      return false;
    }
    this.conn.resetBackoff();
    setUnavailable(this.devices, false);
    await this.registerSelf();
    return true;
  }

  logout(): void {
    this.conn.disconnect();
    setUnavailable(this.devices, true);
  }

  /** Registers this browser as a push target, then refreshes the list. */
  async registerSelf(): Promise<void> {
    const userId = this.auth.get().userId;
    const deviceId = this.auth.get().deviceId;
    if (!userId || !deviceId) return;
    // No web-push token yet: fcm/apns tokens stay empty and pushes to this
    // device degrade to the provider's logging transport until the real
    // Web-Push transport lands (TODO: 真实推送传输).
    const resp = await this.conn.request(REGISTER_DEVICE, {
      userId,
      deviceId,
      platform: 'web',
      deviceName: uaSummary(navigator.userAgent),
    });
    setSelfRegistered(this.devices, resp.code === 0);
    await this.refreshDevices();
  }

  async refreshDevices(): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(GET_USER_DEVICES, { userId });
    if (resp.code !== 0) return;
    setDevices(
      this.devices,
      (resp.devices ?? []).map((d: DeviceInfo) => toEntry(d)),
    );
  }

  /** Removes a push target; refreshing our own only drops the badge state. */
  async unregister(deviceId: string): Promise<boolean> {
    const userId = this.auth.get().userId;
    if (!userId) return false;
    const resp = await this.conn.request(UNREGISTER_DEVICE, { userId, deviceId });
    if (resp.code !== 0) return false;
    if (deviceId === this.auth.get().deviceId) {
      setSelfRegistered(this.devices, false);
    }
    await this.refreshDevices();
    return true;
  }

  isSelf(deviceId: string): boolean {
    return deviceId === this.auth.get().deviceId;
  }
}

function toEntry(d: DeviceInfo): DeviceEntry {
  return {
    deviceId: d.deviceId,
    platform: d.platform,
    deviceName: d.deviceName,
    appVersion: d.appVersion,
    osVersion: d.osVersion,
    registeredAt: Number(d.registeredAt),
    isActive: d.isActive,
  };
}
