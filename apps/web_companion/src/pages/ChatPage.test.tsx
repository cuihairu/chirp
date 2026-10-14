import { afterEach, describe, expect, it, vi } from 'vitest';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { MemoryRouter, Route, Routes } from 'react-router-dom';
import { MsgID } from '@chirp/proto/gateway';
import { PresenceNotify, PresenceStatus } from '@chirp/proto/social';
import { ChannelType, ChatMessage, MsgType, Priority, SenderKind } from '@chirp/proto/chat';
import ChatPage from './ChatPage';
import LoginPage from './LoginPage';
import { createServices, ServicesProvider, type Services } from '../api/services';
import { FakeChatConnection } from '../state/test_helpers';
import { zh } from '../i18n/zh';

const enc = (text: string): Uint8Array => new TextEncoder().encode(text);

const chatBody = (over: Partial<ChatMessage> = {}): Uint8Array =>
  ChatMessage.encode(
    ChatMessage.fromPartial({
      messageId: 'm1',
      senderId: 'user_b',
      channelType: ChannelType.PRIVATE,
      channelId: 'user_a|user_b',
      msgType: MsgType.TEXT,
      content: enc('live'),
      timestamp: 100,
      ...over,
    }),
  ).finish();

/** History for the channel the notification click navigates into; everything
 *  else (login, mark-read, presence pings) is a plain OK. */
const historyResponder = async (msgId: MsgID): Promise<unknown> => {
  if (msgId === MsgID.GET_HISTORY_REQ) {
    return {
      code: 0,
      hasMore: false,
      messages: [
        {
          messageId: 'h1',
          senderId: 'user_b',
          channelType: ChannelType.PRIVATE,
          channelId: 'user_a|user_b',
          content: enc('hello from b'),
          timestamp: 100,
        },
      ],
    };
  }
  return { code: 0 };
};

const okResponder = async (): Promise<unknown> => ({ code: 0 });

interface PlaneHarness {
  conn: FakeChatConnection;
  socialConn: FakeChatConnection;
  partyConn: FakeChatConnection;
  voiceConn: FakeChatConnection;
  deviceConn: FakeChatConnection;
  services: Services;
  unmount: () => void;
}

/**
 * ChatPage mounted at /chat with all four degradeable planes injected (a
 * chat-only fake leaves social/party/voice/device null and their mount
 * effects take the early-return arm). The chat login happens before render
 * so auth holds a user id when the plane effects run; the plane logins
 * themselves are ChatPage's job.
 */
const mountChatPage = async (
  options: { socialResponder?: (msgId: MsgID) => Promise<unknown> } = {},
): Promise<PlaneHarness> => {
  const conn = new FakeChatConnection();
  conn.setResponder(historyResponder);
  const socialConn = new FakeChatConnection();
  socialConn.setResponder(options.socialResponder ?? okResponder);
  const partyConn = new FakeChatConnection();
  partyConn.setResponder(okResponder);
  const voiceConn = new FakeChatConnection();
  voiceConn.setResponder(okResponder);
  const deviceConn = new FakeChatConnection();
  deviceConn.setResponder(okResponder);
  const services = createServices({ conn, socialConn, partyConn, voiceConn, deviceConn });
  await services.api.login('user_a');
  const { unmount } = render(
    <ServicesProvider value={services}>
      <MemoryRouter initialEntries={['/chat']}>
        <Routes>
          <Route path="/chat/:channelKey?" element={<ChatPage />} />
          <Route path="/login" element={<LoginPage />} />
        </Routes>
      </MemoryRouter>
    </ServicesProvider>,
  );
  return { conn, socialConn, partyConn, voiceConn, deviceConn, services, unmount };
};

/** jsdom has no Notification; the stub is what ChatPage measures permission
 *  against and what it constructs for each live message. */
const stubNotification = (permission: 'granted' | 'default') => {
  class NotificationStub {
    static permission = permission;
    static instances: NotificationStub[] = [];
    static requestPermissionCalls = 0;
    static requestPermission(): Promise<'granted'> {
      NotificationStub.requestPermissionCalls += 1;
      return Promise.resolve('granted');
    }
    title: string;
    options: { body?: string; tag?: string };
    onclick: (() => void) | null = null;
    closeCalls = 0;
    constructor(title: string, options: { body?: string; tag?: string }) {
      this.title = title;
      this.options = options;
      NotificationStub.instances.push(this);
    }
    close(): void {
      this.closeCalls += 1;
    }
  }
  vi.stubGlobal('Notification', NotificationStub);
  return NotificationStub;
};

const flushAsync = async (): Promise<void> => {
  for (let i = 0; i < 5; i++) await new Promise((resolve) => setTimeout(resolve, 0));
};

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe('ChatPage friend-online toast', () => {
  const onlineBody = (userId: string): Uint8Array =>
    PresenceNotify.encode(PresenceNotify.fromPartial({ userId, status: PresenceStatus.ONLINE })).finish();
  const gameBody = (userId: string, game: string): Uint8Array =>
    PresenceNotify.encode(
      PresenceNotify.fromPartial({ userId, status: PresenceStatus.IN_GAME, statusMessage: game }),
    ).finish();

  it('toasts roster friends coming online; one toast at a time', async () => {
    const { socialConn } = await mountChatPage({
      socialResponder: async (msgId) =>
        msgId === MsgID.GET_FRIEND_LIST_REQ
          ? { code: 0, friends: [{ userId: 'user_b', status: 2 }, { userId: 'user_c', status: 2 }], totalCount: 2 }
          : { code: 0 },
    });
    await waitFor(() =>
      expect(socialConn.requests.some((r) => r.msgId === MsgID.GET_FRIEND_LIST_REQ)).toBe(true),
    );

    act(() => {
      socialConn.emit(MsgID.FRIEND_ONLINE_NOTIFY, onlineBody('user_b'));
    });
    expect(screen.getByTestId('friend-online-toast')).toBeTruthy();
    expect(screen.getByText(zh.social.friendOnlineNow('user_b'))).toBeTruthy();

    // 新事件替换当前 toast（单槽去重，不叠加）。
    act(() => {
      socialConn.emit(MsgID.FRIEND_ONLINE_NOTIFY, gameBody('user_c', 'g1'));
    });
    expect(screen.getByText(zh.social.friendOnlineGame('user_c', 'g1'))).toBeTruthy();
    expect(screen.queryByText(zh.social.friendOnlineNow('user_b'))).toBeNull();
  });
});

describe('ChatPage plane logins', () => {
  it('logs into every degradeable plane and advertises online presence', async () => {
    const { socialConn, partyConn, voiceConn, deviceConn } = await mountChatPage();

    await waitFor(() =>
      expect(socialConn.requests.some((r) => r.msgId === MsgID.SET_PRESENCE_REQ)).toBe(true),
    );
    expect(socialConn.requests[0]?.msgId).toBe(MsgID.LOGIN_REQ);
    expect(partyConn.requests.some((r) => r.msgId === MsgID.LOGIN_REQ)).toBe(true);
    expect(voiceConn.requests.some((r) => r.msgId === MsgID.LOGIN_REQ)).toBe(true);
    expect(deviceConn.requests.some((r) => r.msgId === MsgID.LOGIN_REQ)).toBe(true);
  });

  it('drops the social presence update when unmounted mid-login', async () => {
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    const { socialConn, unmount } = await mountChatPage({
      socialResponder: async () => {
        await gate;
        return { code: 0 };
      },
    });

    // The social LOGIN is parked on the gate; unmount before it resolves so
    // the effect cleanup marks the chain cancelled.
    expect(socialConn.requests.some((r) => r.msgId === MsgID.LOGIN_REQ)).toBe(true);
    unmount();
    release();
    await flushAsync();

    expect(socialConn.requests.some((r) => r.msgId === MsgID.SET_PRESENCE_REQ)).toBe(false);
  });
});

describe('ChatPage desktop notifications', () => {
  it('notifies for an off-screen message and opens the channel on click', async () => {
    const NotificationStub = stubNotification('granted');
    vi.spyOn(window, 'focus').mockImplementation(() => {});
    const { conn } = await mountChatPage();

    act(() => {
      conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody());
    });

    expect(NotificationStub.instances).toHaveLength(1);
    const notification = NotificationStub.instances[0];
    expect(notification.title).toBe('user_b 发来私信');
    expect(notification.options.body).toBe('live');
    expect(notification.options.tag).toBe('p:user_a|user_b');

    act(() => {
      notification.onclick?.();
    });

    // Navigation landed on the message's channel: its history rendered.
    await waitFor(() => expect(screen.getByText('hello from b')).toBeTruthy());
    expect(notification.closeCalls).toBe(1);
  });

  it('requests notification permission from the enable affordance', async () => {
    const NotificationStub = stubNotification('default');
    await mountChatPage();

    fireEvent.click(screen.getByTestId('notify-enable'));

    await waitFor(() => expect(screen.getByText(zh.notify.enabled)).toBeTruthy());
    expect(NotificationStub.requestPermissionCalls).toBe(1);
  });
});

describe('ChatPage sign-out', () => {
  it('logs out of every plane and lands back on the login page', async () => {
    const harness = await mountChatPage();
    const disconnects = [
      vi.spyOn(harness.conn, 'disconnect'),
      vi.spyOn(harness.socialConn, 'disconnect'),
      vi.spyOn(harness.partyConn, 'disconnect'),
      vi.spyOn(harness.voiceConn, 'disconnect'),
      vi.spyOn(harness.deviceConn, 'disconnect'),
    ];

    fireEvent.click(screen.getByText(zh.chat.signOut));

    await waitFor(() => expect(screen.getByText(zh.login.title)).toBeTruthy());
    expect(harness.conn.requests.some((r) => r.msgId === MsgID.LOGOUT_REQ)).toBe(true);
    for (const disconnect of disconnects) expect(disconnect).toHaveBeenCalled();
  });
});

describe('ChatPage system display surfaces (P0)', () => {
  it('shows the marquee for URGENT+ttl and the popup for HIGH, closing on ack', async () => {
    const { conn, unmount } = await mountChatPage();
    conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm-urgent',
        priority: Priority.PRIORITY_URGENT,
        ttlSeconds: 60,
        senderKind: SenderKind.SENDER_SYSTEM,
        content: enc('全服维护 30 分钟'),
      }),
    );
    conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ messageId: 'm-high', priority: Priority.PRIORITY_HIGH, content: enc('全服奖励已发放') }),
    );
    await flushAsync();
    expect(screen.getByTestId('marquee-strip')).toBeTruthy();
    expect(screen.getByText('全服维护 30 分钟')).toBeTruthy();
    const dialog = screen.getByTestId('system-announce-dialog');
    expect(within(dialog).getByText('全服奖励已发放')).toBeTruthy();
    fireEvent.click(within(dialog).getByTestId('announce-ack'));
    expect(screen.queryByTestId('system-announce-dialog')).toBeNull();
    unmount();
  });

  it('raises no system surface for ordinary messages', async () => {
    const { conn, unmount } = await mountChatPage();
    conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm-plain' }));
    await flushAsync();
    expect(screen.queryByTestId('marquee-strip')).toBeNull();
    expect(screen.queryByTestId('system-announce-dialog')).toBeNull();
    unmount();
  });
});
