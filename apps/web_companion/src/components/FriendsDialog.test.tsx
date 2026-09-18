import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { PresenceStatus } from '@chirp/proto/social';
import { renderLoggedIn } from '../test-utils';
import { addFriend, addPendingIn } from '../state/friend_store';
import { FakeChatConnection } from '../state/test_helpers';
import FriendsDialog from './FriendsDialog';
import { zh } from '../i18n/zh';

describe('FriendsDialog', () => {
  /** MUI puts MuiBadge-invisible on the inner badge span, not the testid root. */
  const dotVisible = (testid: string): boolean => {
    const badge = screen.getByTestId(testid).querySelector('.MuiBadge-badge.MuiBadge-dot');
    return badge !== null && !badge.classList.contains('MuiBadge-invisible');
  };

  it('lists friends with live presence dots and refreshes presence on open', async () => {
    const social = new FakeChatConnection();
    social.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_PRESENCE_REQ) {
        return {
          code: 0,
          presences: [
            { userId: 'user_b', status: PresenceStatus.ONLINE, statusMessage: 'hello' },
          ],
        };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<FriendsDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />, {
      socialConn: social,
      prepare: ({ services }) => {
        addFriend(services.friends, 'user_b');
        addFriend(services.friends, 'user_c');
      },
    });

    await waitFor(() =>
      expect(social.requests.some((r) => r.msgId === MsgID.GET_PRESENCE_REQ)).toBe(true),
    );
    await waitFor(() => expect(screen.getByText('user_b')).toBeTruthy());
    // user_b came back ONLINE from the pull; user_c has no snapshot.
    expect(dotVisible('presence-user_b')).toBe(true);
    expect(dotVisible('presence-user_c')).toBe(false);
  });

  it('sends an add request and surfaces the outgoing pending marker', async () => {
    const social = new FakeChatConnection();
    social.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<FriendsDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />, {
      socialConn: social,
    });

    fireEvent.change(screen.getByTestId('friend-add-input').querySelector('input')!, {
      target: { value: 'user_d' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.social.add }));

    await waitFor(() =>
      expect(
        social.requests.some(
          (r) =>
            r.msgId === MsgID.ADD_FRIEND_REQ &&
            (r.req as { targetUserId: string }).targetUserId === 'user_d',
        ),
      ).toBe(true),
    );
    await waitFor(() => expect(screen.getByText(zh.social.pendingOut('user_d'))).toBeTruthy());
  });

  it('rejects a self-add and shows add failures inline', async () => {
    const social = new FakeChatConnection();
    social.setResponder(async () => ({ code: 8 })); // e.g. already friends
    await renderLoggedIn(<FriendsDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />, {
      socialConn: social,
    });

    const input = screen.getByTestId('friend-add-input').querySelector('input')!;
    fireEvent.change(input, { target: { value: 'user_a' } });
    fireEvent.click(screen.getByRole('button', { name: zh.social.add }));
    expect(screen.getByText(zh.social.addSelf)).toBeTruthy();

    fireEvent.change(input, { target: { value: 'user_b' } });
    fireEvent.click(screen.getByRole('button', { name: zh.social.add }));
    await waitFor(() => expect(screen.getByText(zh.social.addFailed)).toBeTruthy());
  });

  it('accepting a pending request books the sender into the roster', async () => {
    const social = new FakeChatConnection();
    social.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<FriendsDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />, {
      socialConn: social,
      prepare: ({ services }) => {
        addPendingIn(services.friends, 'req-1', 'user_b');
      },
    });
    expect(screen.getByText(zh.social.requestFrom('user_b'))).toBeTruthy();

    fireEvent.click(screen.getByTestId('accept-user_b'));
    await waitFor(() =>
      expect(social.requests.some((r) => r.msgId === MsgID.FRIEND_REQUEST_ACTION_REQ)).toBe(true),
    );
    // The request row is gone and user_b joined the friend list.
    await waitFor(() =>
      expect(screen.queryByText(zh.social.requestFrom('user_b'))).toBeNull(),
    );
    await waitFor(() => expect(screen.getByText('user_b')).toBeTruthy());
  });

  it('opens a private channel when messaging a friend', async () => {
    const onOpenChannel = vi.fn();
    const social = new FakeChatConnection();
    social.setResponder(async () => ({ code: 0 })); // the open-effect presence pull
    await renderLoggedIn(
      <FriendsDialog open onClose={vi.fn()} onOpenChannel={onOpenChannel} />,
      {
        socialConn: social,
        prepare: ({ services }) => {
          addFriend(services.friends, 'user_b');
        },
      },
    );
    await waitFor(() => expect(screen.getByText('user_b')).toBeTruthy());

    fireEvent.click(screen.getByRole('button', { name: zh.social.message }));
    expect(onOpenChannel).toHaveBeenCalledWith('p:user_a|user_b');
  });
});
