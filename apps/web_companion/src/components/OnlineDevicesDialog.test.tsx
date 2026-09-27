import { describe, expect, it, vi } from 'vitest';
import { screen } from '@testing-library/react';
import { DevicesPresenceNotify } from '@chirp/proto/auth';
import { MsgID } from '@chirp/proto/gateway';
import { renderLoggedIn } from '../test-utils';
import { applyDevicePresence } from '../state/online_devices_store';
import { zh } from '../i18n/zh';
import OnlineDevicesDialog, { OnlineDevicesButton } from './OnlineDevicesDialog';

describe('OnlineDevicesDialog', () => {
  it('lists devices from the store with the online copy', async () => {
    await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, {
      prepare: ({ services }) => {
        applyDevicePresence(
          services.onlineDevices,
          { platform: 'ios', deviceId: 'p1', online: true, ts: 0 },
          1,
        );
      },
    });

    expect(screen.getByTestId('online-device-ios')).toBeTruthy();
    expect(screen.getByText('ios · p1')).toBeTruthy();
    expect(screen.getByText(zh.onlineDevices.online)).toBeTruthy();
    expect(screen.queryByTestId('online-devices-empty')).toBeNull();
  });

  it('shows the empty copy when no other device is online', async () => {
    await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />);
    expect(screen.getByTestId('online-devices-empty')).toBeTruthy();
  });

  it('entry button badge counts only live devices', async () => {
    await renderLoggedIn(<OnlineDevicesButton onClick={vi.fn()} />, {
      prepare: ({ services }) => {
        applyDevicePresence(
          services.onlineDevices,
          { platform: 'ios', deviceId: 'p1', online: true, ts: 0 },
          1,
        );
        applyDevicePresence(
          services.onlineDevices,
          { platform: 'android', deviceId: 'p2', online: false, ts: 0 },
          2,
        );
      },
    });
    // 只有在线的端计数;离线的 android 不计。
    expect(screen.getByTestId('online-devices-entry-badge').textContent).toContain('1');
  });

  it('a DEVICES_PRESENCE_NOTIFY through the chat api lands in the store', async () => {
    const { services, conn } = await renderLoggedIn(
      <OnlineDevicesDialog open={false} onClose={vi.fn()} />,
    );
    const body = DevicesPresenceNotify.encode(
      DevicesPresenceNotify.fromPartial({
        devices: [{ platform: 'ios', deviceId: 'phone-9', online: true, ts: 1727400000000 }],
      }),
    ).finish();
    conn.emit(MsgID.DEVICES_PRESENCE_NOTIFY, body);
    await vi.waitFor(() => {
      expect(services.onlineDevices.get().byPlatform['ios']).toMatchObject({
        deviceId: 'phone-9',
        online: true,
      });
    });
  });
});
