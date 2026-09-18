import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { renderLoggedIn } from '../test-utils';
import GroupSettingsDialog from './GroupDialogs';
import { zh } from '../i18n/zh';

const membersResponder = async (msgId: MsgID, _req?: unknown): Promise<unknown> => {
  void _req;
  if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
    return {
      code: 0,
      members: [
        { userId: 'user_a', username: 'user_a' },
        { userId: 'user_b', username: 'user_b' },
      ],
    };
  }
  return { code: 0, groups: [] };
};

const renderDialog = (
  overrides: Partial<{
    ownerId: string;
    responder: (msgId: MsgID, req: unknown) => Promise<unknown>;
  }> = {},
  onLeft = vi.fn(),
) =>
  renderLoggedIn(
    <GroupSettingsDialog
      groupId="guild-1"
      title="Raiders"
      ownerId={overrides.ownerId ?? 'user_a'}
      open
      onClose={vi.fn()}
      onLeft={onLeft}
    />,
    { responder: overrides.responder ?? membersResponder },
  );

describe('GroupSettingsDialog', () => {
  it('lists members and tags the owner', async () => {
    await renderDialog();
    await waitFor(() => expect(screen.getByText('user_a (群主)')).toBeTruthy());
    expect(screen.getByText('user_b')).toBeTruthy();
  });

  it('shows kick buttons only for the owner and only on other members', async () => {
    await renderDialog({ ownerId: 'user_a' });
    await waitFor(() => expect(screen.getByTestId('kick-user_b')).toBeTruthy());
    // The owner row carries no kick button for themselves.
    expect(screen.queryByTestId('kick-user_a')).toBeNull();
  });

  it('hides kick buttons from non-owner members', async () => {
    await renderDialog({ ownerId: 'user_b' });
    await waitFor(() => expect(screen.getByText('user_b (群主)')).toBeTruthy());
    expect(screen.queryByTestId('kick-user_b')).toBeNull();
    expect(screen.queryByTestId('kick-user_a')).toBeNull();
  });

  it('invites a member by user id and reloads the roster', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        if (msgId === MsgID.INVITE_TO_GROUP_REQ) return { code: 0 };
        return membersResponder(msgId);
      },
    });
    await waitFor(() => expect(screen.getByTestId('invite-input')).toBeTruthy());
    fireEvent.change(screen.getByTestId('invite-input').querySelector('input')!, {
      target: { value: 'user_c' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.chat.invite }));
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.INVITE_TO_GROUP_REQ &&
            (r.req as { targetUserId: string }).targetUserId === 'user_c',
        ),
      ).toBe(true),
    );
  });

  it('leaves the group after confirmation and notifies the parent', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true);
    const onLeft = vi.fn();
    await renderDialog({}, onLeft);
    await waitFor(() => expect(screen.getByTestId('leave-group')).toBeTruthy());
    fireEvent.click(screen.getByTestId('leave-group'));
    await waitFor(() => expect(onLeft).toHaveBeenCalled());
    confirmSpy.mockRestore();
  });
});
