import { describe, expect, it } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { ChannelType, ChatMessage, MsgType } from '@chirp/proto/chat';
import { renderLoggedIn } from '../test-utils';
import ChatWindow from './ChatWindow';
import { upsertConversation } from '../state/conversation_store';

const KEY = 'p:user_a|user_b';
const enc = (text: string): Uint8Array => new TextEncoder().encode(text);

const chatBody = (over: Partial<ChatMessage>): Uint8Array =>
  ChatMessage.encode(
    ChatMessage.fromPartial({
      messageId: 'mx',
      senderId: 'user_b',
      channelType: ChannelType.PRIVATE,
      channelId: 'user_a|user_b',
      msgType: MsgType.TEXT,
      content: enc('live'),
      timestamp: 200,
      ...over,
    }),
  ).finish();

const historyResponder = async (msgId: MsgID): Promise<unknown> => {
  if (msgId === MsgID.GET_HISTORY_REQ) {
    return {
      code: 0,
      hasMore: false,
      messages: [
        {
          messageId: 'm1',
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

const seededConversation = {
  kind: 'private' as const,
  key: KEY,
  channelId: 'user_a|user_b',
  peerId: 'user_b',
  title: 'user_b',
  unreadLocal: 5,
};

describe('ChatWindow', () => {
  it('pages history, marks it read and clears the local badge on open', async () => {
    const { services, conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyResponder,
      prepare: ({ services: s }) => upsertConversation(s.conversations, seededConversation),
    });

    await waitFor(() =>
      expect(conn.requests.some((r) => r.msgId === MsgID.GET_HISTORY_REQ)).toBe(true),
    );
    await waitFor(() => expect(screen.getByText('hello from b')).toBeTruthy());
    await waitFor(() => expect(services.conversations.get().conversations[0].unreadLocal).toBe(0));
    expect(conn.requests.some((r) => r.msgId === MsgID.MARK_READ_REQ)).toBe(true);
  });

  it('sends typed text with Enter and shows it immediately', async () => {
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyResponder,
    });

    const input = await waitFor(() => {
      const box = screen.getByTestId('message-input').querySelector('textarea');
      expect(box).toBeTruthy();
      return box as HTMLTextAreaElement;
    });
    fireEvent.change(input, { target: { value: 'my turn' } });
    fireEvent.keyDown(input, { key: 'Enter' });

    await waitFor(() => expect(screen.getByText('my turn')).toBeTruthy());
    const send = conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ);
    expect(send).toBeTruthy();
    expect(send!.req).toMatchObject({ senderId: 'user_a', receiverId: 'user_b' });
  });

  it('marks incoming live messages read while the channel stays open', async () => {
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyResponder,
    });

    await waitFor(() =>
      expect(conn.requests.some((r) => r.msgId === MsgID.GET_HISTORY_REQ)).toBe(true),
    );
    const before = conn.requests.filter((r) => r.msgId === MsgID.MARK_READ_REQ).length;
    conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm2' }));

    await waitFor(() => expect(screen.getByText('live')).toBeTruthy());
    await waitFor(() => {
      const marks = conn.requests.filter((r) => r.msgId === MsgID.MARK_READ_REQ);
      expect(marks.length).toBeGreaterThan(before);
      expect(
        marks.some((r) => (r.req as { messageId: string }).messageId === 'm2'),
      ).toBe(true);
    });
  });
});
