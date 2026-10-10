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

describe('GroupSettingsDialog mute', () => {
  /** user_a = MODERATOR(self), user_b = plain member currently muted. */
  const moderatorResponder = async (msgId: MsgID, _req?: unknown): Promise<unknown> => {
    void _req;
    if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
      return {
        code: 0,
        members: [
          { userId: 'user_a', username: 'user_a', role: 1 },
          { userId: 'user_b', username: 'user_b', mutedUntilTs: Date.now() + 60_000 },
        ],
      };
    }
    return { code: 0, groups: [] };
  };

  /** Same roster but user_b is NOT muted — the mute entry reads 禁言. */
  const unmutedModeratorResponder = async (msgId: MsgID, req?: unknown): Promise<unknown> => {
    if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
      return {
        code: 0,
        members: [
          { userId: 'user_a', username: 'user_a', role: 1 },
          { userId: 'user_b', username: 'user_b' },
        ],
      };
    }
    return membersResponder(msgId, req);
  };

  it('shows the muted badge and an unmute entry to a moderator', async () => {
    await renderDialog({ responder: moderatorResponder });
    await waitFor(() => expect(screen.getByText('user_b (禁言中)')).toBeTruthy());
    expect(screen.getByTestId('mute-user_b').textContent).toBe(zh.chat.unmute);
    // Self (moderator) carries no mute entry on their own row.
    expect(screen.queryByTestId('mute-user_a')).toBeNull();
  });

  it('hides mute entries from plain members', async () => {
    const plainResponder = async (msgId: MsgID, req?: unknown): Promise<unknown> => {
      if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
        return {
          code: 0,
          members: [
            { userId: 'user_a', username: 'user_a' },
            { userId: 'user_b', username: 'user_b', mutedUntilTs: Date.now() + 60_000 },
          ],
        };
      }
      return membersResponder(msgId, req);
    };
    await renderDialog({ responder: plainResponder });
    await waitFor(() => expect(screen.getByText('user_b (禁言中)')).toBeTruthy());
    expect(screen.queryByTestId('mute-user_b')).toBeNull();
  });

  it('mutes via the duration prompt (minutes -> seconds)', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('10');
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        if (msgId === MsgID.SET_GROUP_MUTE_REQ) return { code: 0, mutedUntilTs: 1 };
        return unmutedModeratorResponder(msgId);
      },
    });
    await waitFor(() => expect(screen.getByTestId('mute-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('mute-user_b'));
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.SET_GROUP_MUTE_REQ &&
            (r.req as { targetUserId: string }).targetUserId === 'user_b' &&
            (r.req as { durationSec: number }).durationSec === 600,
        ),
      ).toBe(true),
    );
    promptSpy.mockRestore();
  });

  it('cancels the mute when the prompt is dismissed or zero', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('0');
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        return unmutedModeratorResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('mute-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('mute-user_b'));
    expect(seen.some((r) => r.msgId === MsgID.SET_GROUP_MUTE_REQ)).toBe(false);
    promptSpy.mockRestore();
  });

  it('unmutes a muted member without prompting', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        if (msgId === MsgID.SET_GROUP_MUTE_REQ) return { code: 0, mutedUntilTs: 0 };
        return moderatorResponder(msgId);
      },
    });
    await waitFor(() => expect(screen.getByTestId('mute-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('mute-user_b'));
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.SET_GROUP_MUTE_REQ &&
            (r.req as { durationSec: number }).durationSec === 0,
        ),
      ).toBe(true),
    );
  });

  it('surfaces a failure hint when the server rejects the mute', async () => {
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('10');
    await renderDialog({
      responder: async (msgId, req) => {
        if (msgId === MsgID.SET_GROUP_MUTE_REQ) return { code: 3 };
        return unmutedModeratorResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('mute-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('mute-user_b'));
    await waitFor(() => expect(screen.getByText(zh.chat.muteFailed)).toBeTruthy());
    promptSpy.mockRestore();
  });
});

describe('GroupSettingsDialog alias', () => {
  /** user_a = MODERATOR(self) with no alias; user_b = plain member aliased. */
  const aliasResponder = async (msgId: MsgID, _req?: unknown): Promise<unknown> => {
    void _req;
    if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
      return {
        code: 0,
        members: [
          { userId: 'user_a', username: 'user_a', role: 1 },
          { userId: 'user_b', username: 'user_b', alias: '苍老师' },
        ],
      };
    }
    return { code: 0, groups: [] };
  };

  it('shows the alias in the roster with the real name underneath', async () => {
    await renderDialog({ responder: aliasResponder });
    await waitFor(() => expect(screen.getByText('苍老师')).toBeTruthy());
    expect(screen.getByText('user_b')).toBeTruthy();
  });

  it('lets a member set their own alias via the prompt', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('新昵称');
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        return aliasResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('alias-user_a')).toBeTruthy());
    fireEvent.click(screen.getByTestId('alias-user_a'));
    expect(promptSpy).toHaveBeenCalledWith(zh.chat.aliasPrompt('user_a'), '');
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.SET_MEMBER_ALIAS_REQ &&
            (r.req as { targetUserId: string }).targetUserId === 'user_a' &&
            (r.req as { alias: string }).alias === '新昵称',
        ),
      ).toBe(true),
    );
    promptSpy.mockRestore();
  });

  it('lets a moderator set another member alias', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('苍老师二号');
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        return aliasResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('alias-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('alias-user_b'));
    // The current alias is prefilled into the prompt.
    expect(promptSpy).toHaveBeenCalledWith(zh.chat.aliasPrompt('user_b'), '苍老师');
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.SET_MEMBER_ALIAS_REQ &&
            (r.req as { alias: string }).alias === '苍老师二号',
        ),
      ).toBe(true),
    );
    promptSpy.mockRestore();
  });

  it('hides alias entries for others from plain members', async () => {
    const plainResponder = async (msgId: MsgID, _req?: unknown): Promise<unknown> => {
      void _req;
      if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
        return {
          code: 0,
          members: [
            { userId: 'user_a', username: 'user_a' },
            { userId: 'user_b', username: 'user_b', alias: '苍老师' },
          ],
        };
      }
      return { code: 0, groups: [] };
    };
    await renderDialog({ responder: plainResponder });
    await waitFor(() => expect(screen.getByText('苍老师')).toBeTruthy());
    // Own row still carries the entry (permission = 本人); others' does not.
    expect(screen.getByTestId('alias-user_a')).toBeTruthy();
    expect(screen.queryByTestId('alias-user_b')).toBeNull();
  });

  it('clears the alias when the prompt returns empty', async () => {
    const seen: Array<{ msgId: MsgID; req: unknown }> = [];
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('');
    await renderDialog({
      responder: async (msgId, req) => {
        seen.push({ msgId, req });
        return aliasResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('alias-user_b')).toBeTruthy());
    fireEvent.click(screen.getByTestId('alias-user_b'));
    await waitFor(() =>
      expect(
        seen.some(
          (r) =>
            r.msgId === MsgID.SET_MEMBER_ALIAS_REQ && (r.req as { alias: string }).alias === '',
        ),
      ).toBe(true),
    );
    promptSpy.mockRestore();
  });

  it('surfaces a failure hint when the server rejects the alias', async () => {
    const promptSpy = vi.spyOn(window, 'prompt').mockReturnValue('新昵称');
    await renderDialog({
      responder: async (msgId, req) => {
        if (msgId === MsgID.SET_MEMBER_ALIAS_REQ) return { code: 3 };
        return aliasResponder(msgId, req);
      },
    });
    await waitFor(() => expect(screen.getByTestId('alias-user_a')).toBeTruthy());
    fireEvent.click(screen.getByTestId('alias-user_a'));
    await waitFor(() => expect(screen.getByText(zh.chat.aliasFailed)).toBeTruthy());
    promptSpy.mockRestore();
  });
});
