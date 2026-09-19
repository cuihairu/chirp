import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { renderLoggedIn } from '../test-utils';
import { setDevices, setSelfRegistered, type DeviceEntry } from '../state/device_store';
import { FakeChatConnection } from '../state/test_helpers';
import { zh } from '../i18n/zh';
import DevicesDialog, { DevicesButton } from './DevicesDialog';

const entry = (id: string, name: string, platform: string): DeviceEntry => ({
  deviceId: id,
  platform,
  deviceName: name,
  appVersion: '',
  osVersion: '',
  registeredAt: 1700000000000,
  isActive: true,
});

describe('DevicesDialog', () => {
  it('lists registered devices and marks this browser', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<DevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
      prepare: ({ services }) => {
        // The self marker comes from the auth store's per-browser device id.
        const selfId = services.auth.get().deviceId;
        setDevices(services.devices, [
          entry(selfId, 'Chrome · Linux', 'web'),
          entry('phone-1', 'Pixel 8', 'android'),
        ]);
        setSelfRegistered(services.devices, true);
      },
    });

    expect(screen.getByText(`Chrome · Linux (${zh.device.thisDevice})`)).toBeTruthy();
    expect(screen.getByText('Pixel 8')).toBeTruthy();
    expect(screen.getByTestId('device-remove-phone-1')).toBeTruthy();
    expect(screen.queryByText(zh.device.notRegistered)).toBeNull();
  });

  it('unregisters a device behind a confirm', async () => {
    const confirm = vi.spyOn(window, 'confirm').mockReturnValue(true);
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.UNREGISTER_DEVICE_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', deviceId: 'phone-1' });
        return { code: 0 };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<DevicesDialog open onClose={vi.fn()} />, {
      deviceConn,
      prepare: ({ services }) => {
        setDevices(services.devices, [entry('phone-1', 'Pixel 8', 'android')]);
        setSelfRegistered(services.devices, true);
      },
    });

    fireEvent.click(screen.getByTestId('device-remove-phone-1'));
    await waitFor(() =>
      expect(
        deviceConn.requests.some(
          (r) => r.msgId === MsgID.UNREGISTER_DEVICE_REQ,
        ),
      ).toBe(true),
    );
    expect(confirm).toHaveBeenCalledWith(zh.device.unregisterConfirm('Pixel 8'));
    confirm.mockRestore();
  });

  it('flags the unregistered self state and shows the push note', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<DevicesDialog open onClose={vi.fn()} />, { deviceConn });

    expect(screen.getByText(zh.device.notRegistered)).toBeTruthy();
    expect(screen.getByText(zh.device.pushNote)).toBeTruthy();
  });
});

describe('DevicesButton', () => {
  /** MUI puts MuiBadge-invisible on the inner badge span, not the testid root. */
  const dotVisible = (testid: string): boolean => {
    const badge = screen.getByTestId(testid).querySelector('.MuiBadge-badge.MuiBadge-dot');
    return badge !== null && !badge.classList.contains('MuiBadge-invisible');
  };

  it('warns (visible dot) until this browser is registered', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<DevicesButton onClick={vi.fn()} />, {
      deviceConn,
      prepare: ({ services }) => {
        setSelfRegistered(services.devices, false);
      },
    });
    expect(dotVisible('device-entry-badge')).toBe(true);
  });

  it('hides the dot once this browser is registered', async () => {
    const deviceConn = new FakeChatConnection();
    deviceConn.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<DevicesButton onClick={vi.fn()} />, {
      deviceConn,
      prepare: ({ services }) => {
        setSelfRegistered(services.devices, true);
      },
    });
    expect(dotVisible('device-entry-badge')).toBe(false);
  });
});
