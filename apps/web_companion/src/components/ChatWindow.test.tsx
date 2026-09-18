import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { ChannelType, ChatMessage, MsgType, TypingIndicator } from '@chirp/proto/chat';
import { renderLoggedIn } from '../test-utils';
import ChatWindow from './ChatWindow';
import { zh } from '../i18n/zh';
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

// History legitimately contains own messages (they are paged back from the
// server); live notifies never carry them (no self-echo).
const historyWithOwnResponder = async (msgId: MsgID): Promise<unknown> => {
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
        {
          messageId: 'm2',
          senderId: 'user_a',
          channelType: ChannelType.PRIVATE,
          channelId: 'user_a|user_b',
          content: enc('mine'),
          timestamp: 300,
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

describe('ChatWindow C8', () => {
  const typingBody = (isTyping: boolean): Uint8Array =>
    TypingIndicator.encode(
      TypingIndicator.fromPartial({
        channelType: ChannelType.PRIVATE,
        channelId: 'user_a|user_b',
        userId: 'user_b',
        username: 'user_b',
        isTyping,
      }),
    ).finish();

  it('shows and clears the typing row from indicators', async () => {
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyResponder,
    });
    await waitFor(() => expect(screen.getByTestId('message-input')).toBeTruthy());

    conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, typingBody(true));
    await waitFor(() => expect(screen.getByTestId('typing-row').textContent).toContain('user_b'));
    conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, typingBody(false));
    await waitFor(() => expect(screen.queryByTestId('typing-row')).toBeNull());
  });

  it('edits an own message through the dialog', async () => {
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: async (msgId) =>
        msgId === MsgID.EDIT_MESSAGE_REQ ? { code: 0 } : await historyWithOwnResponder(msgId),
    });
    await waitFor(() => expect(screen.getByTestId('edit-m2')).toBeTruthy());

    fireEvent.click(screen.getByTestId('edit-m2'));
    const dialogInput = await waitFor(() => {
      const box = screen.getByRole('dialog').querySelector('textarea');
      expect(box).toBeTruthy();
      return box as HTMLTextAreaElement;
    });
    expect(dialogInput.value).toBe('mine');
    fireEvent.change(dialogInput, { target: { value: 'mine, fixed' } });
    fireEvent.click(screen.getByText(zh.chat.save).closest('button')!);

    await waitFor(() => {
      const edit = conn.requests.find((r) => r.msgId === MsgID.EDIT_MESSAGE_REQ);
      expect(edit).toBeTruthy();
      expect(new TextDecoder().decode((edit!.req as { newContent: Uint8Array }).newContent)).toBe(
        'mine, fixed',
      );
    });
    await waitFor(() => expect(screen.getByText('mine, fixed')).toBeTruthy());
  });

  it('deletes an own message after confirmation', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true);
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyWithOwnResponder,
    });
    await waitFor(() => expect(screen.getByTestId('delete-m2')).toBeTruthy());

    fireEvent.click(screen.getByTestId('delete-m2'));
    expect(confirmSpy).toHaveBeenCalled();
    await waitFor(() => {
      const del = conn.requests.find((r) => r.msgId === MsgID.DELETE_MESSAGE_REQ);
      expect((del!.req as { messageId: string }).messageId).toBe('m2');
    });
    await waitFor(() => expect(screen.getByText(zh.chat.deleted)).toBeTruthy());
    confirmSpy.mockRestore();
  });

  it('fires throttled typing starts while composing', async () => {
    const { conn } = await renderLoggedIn(<ChatWindow channelKey={KEY} />, {
      responder: historyResponder,
    });
    const input = await waitFor(() => {
      const box = screen.getByTestId('message-input').querySelector('textarea');
      expect(box).toBeTruthy();
      return box as HTMLTextAreaElement;
    });
    fireEvent.change(input, { target: { value: 't' } });
    fireEvent.change(input, { target: { value: 'ty' } });
    // One start for the burst (3s throttle), encoded as a 2208 frame.
    const starts = conn.requests.filter(
      (r) => r.msgId === MsgID.TYPING_INDICATOR_NOTIFY && TypingIndicator.decode(r.req as Uint8Array).isTyping,
    );
    expect(starts).toHaveLength(1);
  });

  it('opens group settings from the header for group channels only', async () => {
    const groupKey = 'g:guild-1';
    await renderLoggedIn(<ChatWindow channelKey={groupKey} />, {
      responder: async (msgId) =>
        msgId === MsgID.GET_HISTORY_REQ
          ? {
              code: 0,
              hasMore: false,
              messages: [
                {
                  messageId: 'm1',
                  senderId: 'user_b',
                  channelType: ChannelType.GUILD,
                  channelId: 'guild-1',
                  content: enc('hi guild'),
                  timestamp: 100,
                },
              ],
            }
          : msgId === MsgID.GET_GROUP_MEMBERS_REQ
            ? { code: 0, members: [{ userId: 'user_a' }, { userId: 'user_b' }] }
            : { code: 0, groups: [{ groupId: 'guild-1', groupName: 'Raiders', ownerId: 'user_a' }] },
      prepare: ({ services: s }) =>
        upsertConversation(s.conversations, {
          kind: 'group',
          key: groupKey,
          channelId: 'guild-1',
          peerId: 'guild-1',
          title: 'Raiders',
          ownerId: 'user_a',
          unreadLocal: 0,
        }),
    });
    await waitFor(() => expect(screen.getByText('hi guild')).toBeTruthy());

    // Group channels carry the settings entry; private ones do not.
    expect(screen.getByTestId('group-settings')).toBeTruthy();
    fireEvent.click(screen.getByTestId('group-settings'));
    await waitFor(() => expect(screen.getByText(zh.chat.groupSettings('Raiders'))).toBeTruthy());
    await waitFor(() => expect(screen.getByTestId('kick-user_b')).toBeTruthy());
  });
});

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
