import { describe, expect, it } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import App from '../App';
import { createServices } from '../api/services';
import { zh } from '../i18n/zh';
import { FakeChatConnection } from '../state/test_helpers';

const mount = (conn: FakeChatConnection) =>
  render(<App services={createServices({ conn })} />);

describe('LoginPage', () => {
  it('logs in and moves to the chat shell', async () => {
    const conn = new FakeChatConnection();
    conn.setResponder(async () => ({ code: 0 }));
    mount(conn);

    fireEvent.change(screen.getByLabelText(zh.login.userIdLabel), {
      target: { value: 'user_1' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.login.submit }));

    await waitFor(() => expect(screen.getByText('user_1')).toBeTruthy()); // chat shell header shows the user id
    expect(conn.requests.some((r) => r.msgId === MsgID.LOGIN_REQ)).toBe(true);
  });

  it('surfaces the server error code as text', async () => {
    const conn = new FakeChatConnection();
    conn.setResponder(async (msgId) =>
      msgId === MsgID.LOGIN_REQ ? { code: 8 } : { code: 0 },
    );
    mount(conn);

    fireEvent.change(screen.getByLabelText(zh.login.userIdLabel), {
      target: { value: 'user_1' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.login.submit }));

    // ErrorCode 8 = RATE_LIMITED.
    await waitFor(() => expect(screen.getByText('操作过于频繁,请稍后再试')).toBeTruthy());
  });

  it('shows a connection failure instead of crashing', async () => {
    const conn = new FakeChatConnection();
    conn.failNextRequest();
    mount(conn);

    fireEvent.change(screen.getByLabelText(zh.login.userIdLabel), {
      target: { value: 'user_1' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.login.submit }));

    await waitFor(() => expect(screen.getByText('无法连接到服务器,请稍后再试')).toBeTruthy());
  });

  it('keeps the submit button disabled for blank input', () => {
    const conn = new FakeChatConnection();
    mount(conn);
    expect(
      (screen.getByRole('button', { name: zh.login.submit }) as HTMLButtonElement).disabled,
    ).toBe(true);
  });
});
