import { describe, expect, it } from 'vitest';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import App from '../App';
import { createServices } from '../api/services';
import { zh } from '../i18n/zh';
import { FakeChatConnection } from '../state/test_helpers';

const mountLoggedIn = async () => {
  const conn = new FakeChatConnection();
  conn.setResponder(async () => ({ code: 0 }));
  const services = createServices({ conn });
  render(<App services={services} />);
  await services.api.login('user_1');
  return { conn, services };
};

describe('ConnectionBanner', () => {
  it('stays hidden while connected', async () => {
    const { conn } = await mountLoggedIn();
    conn.emitStatus('connected');
    expect(screen.queryByText(zh.banner.reconnecting)).toBeNull();
    expect(screen.queryByText(zh.banner.kicked)).toBeNull();
  });

  it('shows the reconnect banner for logged-in users only', async () => {
    const { conn } = await mountLoggedIn();
    act(() => conn.emitStatus('waiting-reconnect'));
    expect(screen.getByText(zh.banner.reconnecting)).toBeTruthy();
    act(() => conn.emitStatus('connected'));
    expect(screen.queryByText(zh.banner.reconnecting)).toBeNull();
  });

  it('kicked sessions flag auth and go back to login', async () => {
    const { conn, services } = await mountLoggedIn();
    act(() => conn.emitStatus('kicked'));
    expect(screen.getByText(zh.banner.kicked)).toBeTruthy();
    expect(services.auth.get().kicked).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: zh.banner.backToLogin }));
    await waitFor(() => expect(screen.getByText(zh.login.title)).toBeTruthy());
    expect(services.auth.get()).toMatchObject({ userId: null, loggedIn: false, kicked: false });
  });
});
