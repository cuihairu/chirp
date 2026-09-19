import { describe, expect, it } from 'vitest';
import { MsgID } from '@chirp/proto/gateway';
import { DeviceInfo } from '@chirp/proto/notification';
import { DeviceApi } from './device_api';
import { createStore } from '../state/store';
import { createDeviceStore } from '../state/device_store';
import type { AuthState } from '../state/auth_store';
import { FakeChatConnection } from '../state/test_helpers';

const device = (id: string, overrides: Partial<DeviceInfo> = {}) =>
  DeviceInfo.fromPartial({
    deviceId: id,
    userId: 'user_a',
    platform: 'web',
    deviceName: 'Chrome · Linux',
    registeredAt: 1700000000000,
    isActive: true,
    ...overrides,
  });

const makeHarness = () => {
  const conn = new FakeChatConnection();
  const auth = createStore<AuthState>({
    userId: 'user_a',
    kicked: false,
    deviceId: 'browser-1',
    loggedIn: true,
  });
  const devices = createDeviceStore();
  const api = new DeviceApi({ conn, auth, devices });
  // Default green responder; device list answers with this browser only.
  conn.setResponder(async (msgId) => {
    if (msgId === MsgID.GET_USER_DEVICES_REQ) {
      return { code: 0, devices: [device('browser-1')] };
    }
    return { code: 0 };
  });
  return { conn, auth, devices, api };
};

describe('DeviceApi.login', () => {
  it('logs in with the browser device id and registers self', async () => {
    const h = makeHarness();
    let registerBody: unknown;
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.LOGIN_REQ) {
        expect(req).toMatchObject({ token: 'user_a', deviceId: 'browser-1', platform: 'web' });
        return { code: 0 };
      }
      if (msgId === MsgID.REGISTER_DEVICE_REQ) {
        registerBody = req;
        return { code: 0 };
      }
      if (msgId === MsgID.GET_USER_DEVICES_REQ) {
        return { code: 0, devices: [device('browser-1'), device('phone-1', { platform: 'android' })] };
      }
      return { code: 0 };
    });
    expect(await h.api.login('user_a')).toBe(true);
    expect(registerBody).toMatchObject({ userId: 'user_a', deviceId: 'browser-1', platform: 'web' });
    expect(h.devices.get().selfRegistered).toBe(true);
    expect(h.devices.get().unavailable).toBe(false);
    expect(h.devices.get().devices.map((d) => d.deviceId)).toEqual(['browser-1', 'phone-1']);
  });

  it('degrades to unavailable on a login rejection', async () => {
    const h = makeHarness();
    let disconnected = false;
    h.conn.setResponder(async () => ({ code: 2 }));
    h.conn.disconnect = () => {
      disconnected = true;
    };
    expect(await h.api.login('user_a')).toBe(false);
    expect(disconnected).toBe(true);
    expect(h.devices.get().unavailable).toBe(true);
  });
});

describe('DeviceApi.unregister', () => {
  it('removes another device and refreshes the list', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.UNREGISTER_DEVICE_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', deviceId: 'phone-1' });
        return { code: 0 };
      }
      if (msgId === MsgID.GET_USER_DEVICES_REQ) {
        return { code: 0, devices: [device('browser-1')] };
      }
      return { code: 0 };
    });
    expect(await h.api.unregister('phone-1')).toBe(true);
    expect(h.devices.get().devices.map((d) => d.deviceId)).toEqual(['browser-1']);
  });

  it('removing our own device drops the selfRegistered badge', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.UNREGISTER_DEVICE_REQ) {
        return { code: 0 };
      }
      if (msgId === MsgID.GET_USER_DEVICES_REQ) {
        return { code: 0, devices: [device('phone-1', { platform: 'android' })] };
      }
      return { code: 0 };
    });
    expect(await h.api.unregister('browser-1')).toBe(true);
    expect(h.devices.get().selfRegistered).toBe(false);
    expect(h.api.isSelf('phone-1')).toBe(false);
    expect(h.api.isSelf('browser-1')).toBe(true);
  });

  it('reports failure without touching the mirror', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.UNREGISTER_DEVICE_REQ) {
        return { code: 7 };
      }
      return { code: 0 };
    });
    expect(await h.api.unregister('phone-1')).toBe(false);
    expect(h.devices.get().devices.map((d) => d.deviceId)).toEqual(['browser-1']);
  });
});

describe('DeviceApi.logout', () => {
  it('disconnects and hides the feature', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    let disconnected = false;
    h.conn.disconnect = () => {
      disconnected = true;
    };
    h.api.logout();
    expect(disconnected).toBe(true);
    expect(h.devices.get().unavailable).toBe(true);
    expect(h.devices.get().devices).toEqual([]);
  });
});

describe('uaSummary', () => {
  it('summarizes common agents and degrades gracefully', async () => {
    const { uaSummary } = await import('./ua_summary');
    expect(uaSummary('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126.0 Safari/537.36')).toBe(
      'Chrome · Linux',
    );
    expect(uaSummary('Mozilla/5.0 (Windows NT 10.0; Win64; x64) Edg/126.0')).toBe('Edge · Windows');
    expect(uaSummary('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/605.1.15')).toBe(
      'Safari · macOS',
    );
    expect(uaSummary('weird-thing/1.0')).toBe('Web');
  });
});
