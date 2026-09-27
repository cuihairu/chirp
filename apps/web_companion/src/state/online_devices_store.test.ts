import { describe, expect, it } from 'vitest';
import {
  applyDevicePresence,
  applyPresenceList,
  createOnlineDevicesStore,
  onlineDevicesOf,
  resetOnlineDevices,
} from './online_devices_store';

const entry = (platform: string, deviceId: string, online: boolean) => ({
  platform,
  deviceId,
  online,
  ts: 1727400000000,
});

describe('online devices store', () => {
  it('seeds from the login list and keeps one slot per platform', () => {
    const store = createOnlineDevicesStore();
    applyPresenceList(store, [entry('ios', 'p1', true), entry('android', 'p2', true)], 1);
    expect(onlineDevicesOf(store.get()).map((d) => d.platform)).toEqual(['android', 'ios']);

    // 同 platform 的新事件覆盖旧条目（同型互顶后只剩一条）。
    applyDevicePresence(store, entry('ios', 'p9', true), 2);
    const ios = store.get().byPlatform['ios'];
    expect(ios.deviceId).toBe('p9');
    expect(ios.at).toBe(2);
    expect(onlineDevicesOf(store.get())).toHaveLength(2);
  });

  it('marks offline without dropping the entry', () => {
    const store = createOnlineDevicesStore();
    applyPresenceList(store, [entry('web', 'tab-1', true)], 1);
    applyDevicePresence(store, entry('web', 'tab-1', false), 2);
    expect(store.get().byPlatform['web']).toMatchObject({
      deviceId: 'tab-1',
      online: false,
      at: 2,
    });
  });

  it('normalizes empty platform to default and resets cleanly', () => {
    const store = createOnlineDevicesStore();
    applyPresenceList(store, [entry('', 'legacy', true)], 1);
    expect(store.get().byPlatform['default']).toMatchObject({ platform: 'default' });

    resetOnlineDevices(store);
    expect(store.get().byPlatform).toEqual({});
  });
});
