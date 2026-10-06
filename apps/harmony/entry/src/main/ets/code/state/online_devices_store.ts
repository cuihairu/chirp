import { createStore, type Store } from './store';
import type { DevicePresence } from '@chirp/proto/auth';

/**
 * 多端在线（P0）：this account's other live sessions, keyed by platform.
 * Seeded from the login response's online_devices (the server already
 * excludes the logging-in session) and updated by DEVICES_PRESENCE_NOTIFY
 * events — online on login, offline on disconnect/kick. One slot per
 * platform (same-platform re-login kicks the previous session), so the map
 * never holds two entries for one platform.
 */
export interface OnlineDeviceView {
  platform: string;
  deviceId: string;
  online: boolean;
  /** Local receive time of the last event for this platform. */
  at: number;
}

export interface OnlineDevicesState {
  byPlatform: Record<string, OnlineDeviceView>;
}

export const createOnlineDevicesStore = (): Store<OnlineDevicesState> =>
  createStore<OnlineDevicesState>({ byPlatform: {} });

const keyOf = (platform: string): string => platform || 'default';

/** Seed (login) or merge (notify) a wire entry into the store. */
export function applyDevicePresence(
  store: Store<OnlineDevicesState>,
  entry: DevicePresence,
  now: number,
): void {
  const key = keyOf(entry.platform);
  store.set((prev) => ({
    byPlatform: {
      ...prev.byPlatform,
      [key]: {
        platform: entry.platform || 'default',
        deviceId: entry.deviceId,
        online: entry.online,
        at: now,
      },
    },
  }));
}

/** A device went offline: keep the entry (last-seen shape) but mark it. */
export const applyPresenceList = (
  store: Store<OnlineDevicesState>,
  devices: DevicePresence[] | undefined,
  now: number,
): void => {
  for (const entry of devices ?? []) applyDevicePresence(store, entry, now);
};

/** Login switch / logout: drop the previous account's mirror entirely. */
export const resetOnlineDevices = (store: Store<OnlineDevicesState>): void => {
  store.set({ byPlatform: {} });
};

export const onlineDevicesOf = (state: OnlineDevicesState): OnlineDeviceView[] =>
  Object.values(state.byPlatform).sort((a, b) => a.platform.localeCompare(b.platform));
