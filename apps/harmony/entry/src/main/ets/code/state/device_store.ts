import { createStore, type Store } from './store';

/** Narrowed copy of chirp.notification.DeviceInfo the UI renders. */
export interface DeviceEntry {
  deviceId: string;
  platform: string;
  deviceName: string;
  appVersion: string;
  osVersion: string;
  registeredAt: number;
  isActive: boolean;
}

/**
 * Mirror of the device plane (app_gateway → notification): which push
 * targets our account has and whether this browser is one of them. The
 * device store is memory-only server-side, so the list is rebuilt on every
 * login; `unavailable` hides the entry while chat keeps working.
 */
export interface DeviceState {
  devices: DeviceEntry[];
  /** This browser completed REGISTER_DEVICE (best-effort). */
  selfRegistered: boolean;
  /** Device plane unreachable / not logged in: entry hides, chat unaffected. */
  unavailable: boolean;
}

export const createDeviceStore = (
  initial: DeviceState = { devices: [], selfRegistered: false, unavailable: false },
): Store<DeviceState> => createStore<DeviceState>(initial);

/** Replaces the list from GET_USER_DEVICES (server is authoritative). */
export function setDevices(store: Store<DeviceState>, devices: DeviceEntry[]): void {
  store.set((prev) => ({ ...prev, devices }));
}

export function setSelfRegistered(store: Store<DeviceState>, selfRegistered: boolean): void {
  store.set((prev) => (prev.selfRegistered === selfRegistered ? prev : { ...prev, selfRegistered }));
}

/** Login failure, kick, or a dead edge: hide the feature, keep chat. */
export function setUnavailable(store: Store<DeviceState>, unavailable: boolean): void {
  store.set((prev) => {
    if (prev.unavailable === unavailable) return prev;
    // Coming back online starts from a clean list; going down forgets it.
    return unavailable
      ? { ...prev, unavailable, devices: [], selfRegistered: false }
      : { ...prev, unavailable };
  });
}
