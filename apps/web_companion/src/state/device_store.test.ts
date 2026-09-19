import { describe, expect, it, vi } from 'vitest';
import {
  createDeviceStore,
  setDevices,
  setSelfRegistered,
  setUnavailable,
  type DeviceEntry,
} from './device_store';

const entry = (id: string, overrides: Partial<DeviceEntry> = {}): DeviceEntry => ({
  deviceId: id,
  platform: 'web',
  deviceName: 'Chrome · Linux',
  appVersion: '',
  osVersion: '',
  registeredAt: 1700000000000,
  isActive: true,
  ...overrides,
});

describe('device store', () => {
  it('replaces the list from the server snapshot', () => {
    const devices = createDeviceStore();
    setDevices(devices, [entry('d1'), entry('d2')]);
    expect(devices.get().devices).toHaveLength(2);
    setDevices(devices, [entry('d3')]);
    expect(devices.get().devices.map((d) => d.deviceId)).toEqual(['d3']);
  });

  it('toggles selfRegistered without touching the list', () => {
    const devices = createDeviceStore();
    setDevices(devices, [entry('d1')]);
    setSelfRegistered(devices, true);
    expect(devices.get().selfRegistered).toBe(true);
    expect(devices.get().devices).toHaveLength(1);
  });

  it('going unavailable clears the mirror; coming back keeps it empty', () => {
    const devices = createDeviceStore();
    setDevices(devices, [entry('d1')]);
    setSelfRegistered(devices, true);

    setUnavailable(devices, true);
    expect(devices.get()).toMatchObject({ unavailable: true, devices: [], selfRegistered: false });

    setUnavailable(devices, false);
    expect(devices.get().unavailable).toBe(false);
    expect(devices.get().devices).toEqual([]);
  });

  it('no-ops do not notify subscribers', () => {
    const devices = createDeviceStore();
    const listener = vi.fn();
    devices.subscribe(listener);
    // setDevices always replaces (server-authoritative, fresh array every
    // pull), so only the boolean setters have no-op detection.
    setSelfRegistered(devices, false);
    setUnavailable(devices, false);
    expect(listener).not.toHaveBeenCalled();

    setSelfRegistered(devices, true);
    expect(listener).toHaveBeenCalledTimes(1);
  });
});
