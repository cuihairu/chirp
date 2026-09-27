import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { DevicesPresenceNotify } from '@chirp/proto/auth';
import { MsgID } from '@chirp/proto/gateway';
import { renderLoggedIn } from '../test-utils';
import { FakeChatConnection } from '../state/test_helpers';
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

describe('OnlineDevicesDialog 游戏在线状态开关', () => {
  it('hides the section when the app_gateway plane is not configured', async () => {
    await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />);
    expect(screen.queryByTestId('game-presence-section')).toBeNull();
  });

  it('opening pulls the authoritative switch and the game list', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async (msgId) => {
      expect(msgId).toBe(MsgID.GET_GAME_PRESENCE_REQ);
      return { code: 0, enabled: true, entries: [{ gameId: 'game_a' }, { gameId: 'game_b' }] };
    });
    const { services } = await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
    });
    await waitFor(() => expect(services.gamePresence.get().loaded).toBe(true));
    expect(screen.getByRole('checkbox')).toHaveProperty('checked', true);
    expect(screen.getByTestId('game-presence-state').textContent).toContain('game_a');
  });

  it('the server default is enabled even without an explicit setting', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0, enabled: true, entries: [] }));
    const { services } = await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
    });
    await waitFor(() => expect(services.gamePresence.get().loaded).toBe(true));
    // 从未绑定/从未设置:开关开着,但提示还没有生效的游戏。
    expect(screen.getByRole('checkbox')).toHaveProperty('checked', true);
    expect(screen.getByTestId('game-presence-state').textContent).toBe(
      zh.gamePresence.noGames,
    );
  });

  it('a disabled account renders the off copy and an unchecked switch', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0, enabled: false, entries: [] }));
    await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, { deviceConn });
    await waitFor(() => expect(screen.getByTestId('game-presence-state')).toBeTruthy());
    expect(screen.getByRole('checkbox')).toHaveProperty('checked', false);
    expect(screen.getByTestId('game-presence-state').textContent).toBe(zh.gamePresence.off);
  });

  it('toggling off writes the switch and refreshes to the cleared list', async () => {
    const deviceConn = new FakeChatConnection();
    let enabled = true;
    deviceConn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.SET_GAME_PRESENCE_ENABLED_REQ) {
        expect(req).toMatchObject({ playerId: '', enabled: false });
        enabled = false;
        return { code: 0 };
      }
      return { code: 0, enabled, entries: enabled ? [{ gameId: 'game_a' }] : [] };
    });
    const { services } = await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
    });
    await waitFor(() => expect(services.gamePresence.get().loaded).toBe(true));
    fireEvent.click(screen.getByRole('checkbox'));
    await waitFor(() => expect(screen.getByRole('checkbox')).toHaveProperty('checked', false));
    expect(services.gamePresence.get()).toMatchObject({ enabled: false, games: [] });
    expect(screen.getByTestId('game-presence-state').textContent).toBe(zh.gamePresence.off);
  });

  it('a rejected write surfaces the failure copy and keeps the state', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async (msgId) =>
      msgId === MsgID.SET_GAME_PRESENCE_ENABLED_REQ
        ? { code: 5 }
        : { code: 0, enabled: true, entries: [{ gameId: 'game_a' }] },
    );
    const { services } = await renderLoggedIn(<OnlineDevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
    });
    await waitFor(() => expect(services.gamePresence.get().loaded).toBe(true));
    fireEvent.click(screen.getByRole('checkbox'));
    await waitFor(() => expect(screen.getByTestId('game-presence-failed')).toBeTruthy());
    expect(screen.getByRole('checkbox')).toHaveProperty('checked', true);
  });

});
