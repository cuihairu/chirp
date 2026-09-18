import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { renderLoggedIn } from '../test-utils';
import ConversationList from './ConversationList';
import { zh } from '../i18n/zh';

describe('ConversationList', () => {
  it('renders conversations with previews and unread badges', async () => {
    await renderLoggedIn(
      <ConversationList activeKey="p:user_a|user_b" onOpen={vi.fn()} />,
      {
        prepare: ({ services }) => {
          services.conversations.set(() => ({
            conversations: [
              {
                kind: 'private',
                key: 'p:user_a|user_b',
                channelId: 'user_a|user_b',
                peerId: 'user_b',
                title: 'user_b',
                lastMessagePreview: 'hi there',
                unreadLocal: 3,
              },
            ],
          }));
        },
      },
    );
    expect(screen.getByText('user_b')).toBeTruthy();
    expect(screen.getByText('hi there')).toBeTruthy();
    expect(screen.getByText('3')).toBeTruthy(); // badge
  });

  it('opens the tapped conversation', async () => {
    const onOpen = vi.fn();
    await renderLoggedIn(<ConversationList onOpen={onOpen} />, {
      prepare: ({ services }) => {
        services.conversations.set(() => ({
          conversations: [
            {
              kind: 'private',
              key: 'p:user_a|user_c',
              channelId: 'user_a|user_c',
              peerId: 'user_c',
              title: 'user_c',
              unreadLocal: 0,
            },
          ],
        }));
      },
    });
    fireEvent.click(screen.getByText('user_c'));
    expect(onOpen).toHaveBeenCalledWith('p:user_a|user_c');
  });

  it('starts a new private chat from the dialog', async () => {
    const onOpen = vi.fn();
    const mounted = await renderLoggedIn(<ConversationList onOpen={onOpen} />);

    fireEvent.click(screen.getByRole('button', { name: zh.chat.newChat }));
    fireEvent.change(screen.getByLabelText(zh.chat.newChatLabel), {
      target: { value: 'user_d' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.chat.confirm }));

    await waitFor(() => expect(onOpen).toHaveBeenCalledWith('p:user_a|user_d'));
    const list = mounted.services.conversations.get().conversations;
    expect(list.find((c) => c.key === 'p:user_a|user_d')).toMatchObject({
      kind: 'private',
      peerId: 'user_d',
      title: 'user_d',
    });
  });

  it('refuses a self-chat', async () => {
    const onOpen = vi.fn();
    await renderLoggedIn(<ConversationList onOpen={onOpen} />);

    fireEvent.click(screen.getByRole('button', { name: zh.chat.newChat }));
    fireEvent.change(screen.getByLabelText(zh.chat.newChatLabel), {
      target: { value: 'user_a' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.chat.confirm }));

    expect(screen.getByText(zh.chat.startChatSelf)).toBeTruthy();
    expect(onOpen).not.toHaveBeenCalled();
  });
});
