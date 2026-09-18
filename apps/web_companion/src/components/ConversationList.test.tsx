import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { PresenceStatus } from '@chirp/proto/social';
import { renderLoggedIn } from '../test-utils';
import { setPresence } from '../state/presence_store';
import { FakeChatConnection } from '../state/test_helpers';
import ConversationList from './ConversationList';
import { zh } from '../i18n/zh';

describe('ConversationList', () => {
  /** MUI puts MuiBadge-invisible on the inner badge span; scope to the dot
   * span because the unread badge is nested inside the same testid root. */
  const dotVisible = (testid: string): boolean => {
    const badge = screen.getByTestId(testid).querySelector('.MuiBadge-badge.MuiBadge-dot');
    return badge !== null && !badge.classList.contains('MuiBadge-invisible');
  };

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

  it('marks online private peers with a presence dot', async () => {
    await renderLoggedIn(
      <ConversationList activeKey="p:user_a|user_b" onOpen={vi.fn()} />,
      {
        socialConn: new FakeChatConnection(),
        prepare: ({ services }) => {
          services.conversations.set(() => ({
            conversations: [
              {
                kind: 'private',
                key: 'p:user_a|user_b',
                channelId: 'user_a|user_b',
                peerId: 'user_b',
                title: 'user_b',
                unreadLocal: 0,
              },
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
          setPresence(services.presence, 'user_b', PresenceStatus.ONLINE, '', Date.now());
        },
      },
    );
    // user_b has a fresh ONLINE snapshot; user_c has none and renders offline.
    expect(dotVisible('presence-user_b')).toBe(true);
    expect(dotVisible('presence-user_c')).toBe(false);
  });

  it('shows the friends entry only when the social plane is configured', async () => {
    const onOpen = vi.fn();
    await renderLoggedIn(<ConversationList onOpen={onOpen} />);
    expect(screen.queryByRole('button', { name: zh.social.title })).toBeNull();

    await renderLoggedIn(<ConversationList onOpen={onOpen} />, {
      socialConn: new FakeChatConnection(),
    });
    expect(screen.getByRole('button', { name: zh.social.title })).toBeTruthy();
  });

  it('creates a group and opens its channel', async () => {
    const onOpen = vi.fn();
    const mounted = await renderLoggedIn(<ConversationList onOpen={onOpen} />, {
      responder: async (msgId) =>
        msgId === MsgID.CREATE_GROUP_REQ ? { code: 0, groupId: 'guild-7' } : { code: 0, groups: [{ groupId: 'guild-7', groupName: 'Party', ownerId: 'user_a' }] },
    });

    fireEvent.click(screen.getByRole('button', { name: zh.chat.newGroup }));
    fireEvent.change(screen.getByTestId('group-name-input').querySelector('input')!, {
      target: { value: 'Party' },
    });
    fireEvent.click(screen.getByTestId('group-create-confirm'));

    await waitFor(() => expect(onOpen).toHaveBeenCalledWith('g:guild-7'));
    const list = mounted.services.conversations.get().conversations;
    expect(list.find((c) => c.key === 'g:guild-7')).toMatchObject({
      kind: 'group',
      title: 'Party',
      ownerId: 'user_a',
    });
  });
});
