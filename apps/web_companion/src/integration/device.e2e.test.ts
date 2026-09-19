import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ChirpClient } from '../protocol/chirp_client';
import {
  GET_USER_DEVICES,
  LOGIN,
  REGISTER_DEVICE,
  UNREGISTER_DEVICE,
} from '../protocol/msg_map';

/**
 * Real-backend round-trips against chirp_app_gateway + chirp_notification.
 * Skipped entirely unless CHIRP_DEVICE_WS_URL is set — scripts/web_smoke.sh
 * starts both when their binaries are present and points this suite at the
 * app_gateway websocket.
 */
const suite = process.env.CHIRP_DEVICE_WS_URL ? describe : describe.skip;

const RUN = Date.now().toString(36);
const USER = `web_dev_${RUN}`;
const DEVICE = `web-e2e-${RUN}`;

let client: ChirpClient;

beforeAll(async () => {
  client = new ChirpClient({ url: process.env.CHIRP_DEVICE_WS_URL as string });
  await client.connect();
  const resp = await client.request(LOGIN, {
    token: USER,
    deviceId: DEVICE,
    platform: 'web',
  });
  expect(resp.code).toBe(0);
  client.resetBackoff();
});

afterAll(() => {
  client?.disconnect();
});

suite('device plane (app_gateway → notification)', () => {  it('registers this device and lists it back with platform web', async () => {
    const resp = await client.request(REGISTER_DEVICE, {
      userId: USER,
      deviceId: DEVICE,
      platform: 'web',
      deviceName: 'web-smoke',
    });
    expect(resp.code).toBe(0);

    const list = await client.request(GET_USER_DEVICES, { userId: USER });
    expect(list.code).toBe(0);
    const mine = (list.devices ?? []).find((d) => d.deviceId === DEVICE);
    expect(mine).toBeTruthy();
    expect(mine?.platform).toBe('web');
    expect(mine?.isActive).toBe(true);
  });

  it('unregisters the device and it disappears from the list', async () => {
    const resp = await client.request(UNREGISTER_DEVICE, {
      userId: USER,
      deviceId: DEVICE,
    });
    expect(resp.code).toBe(0);

    const list = await client.request(GET_USER_DEVICES, { userId: USER });
    expect(list.code).toBe(0);
    expect((list.devices ?? []).some((d) => d.deviceId === DEVICE)).toBe(false);
  });

  it('refuses an unauthenticated device query after logout', async () => {
    const outsider = new ChirpClient({ url: process.env.CHIRP_DEVICE_WS_URL as string });
    await outsider.connect();
    // No LOGIN: the edge must answer AUTH_FAILED instead of forwarding.
    const list = await outsider.request(GET_USER_DEVICES, { userId: USER });
    expect(list.code).not.toBe(0);
    outsider.disconnect();
  });
});
