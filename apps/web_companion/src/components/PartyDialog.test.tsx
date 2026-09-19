import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { PartyInfo } from '@chirp/proto/party';
import { renderLoggedIn } from '../test-utils';
import { addInvite, applySnapshot } from '../state/party_store';
import { FakeChatConnection } from '../state/test_helpers';
import { zh } from '../i18n/zh';
import PartyDialog, { PartyButton } from './PartyDialog';

const partyInfo = () =>
  PartyInfo.fromPartial({
    partyId: 'party-1',
    leaderId: 'user_a',
    maxMembers: 5,
    members: [
      { userId: 'user_a', ready: false },
      { userId: 'user_b', ready: true },
    ],
  });

describe('PartyDialog', () => {
  it('offers creation when partyless and sends the parsed capacity', async () => {
    const party = new FakeChatConnection();
    party.setResponder(async (msgId, req) => {
      if (msgId === MsgID.CREATE_PARTY_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', maxMembers: 3 });
        return { code: 0, party: partyInfo() };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, { partyConn: party });
    expect(screen.getByText(zh.party.noParty)).toBeTruthy();

    fireEvent.change(screen.getByTestId('party-max-members').querySelector('input')!, {
      target: { value: '3' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.party.create }));

    await waitFor(() => expect(screen.getByText(zh.party.members(2, 5))).toBeTruthy());
  });

  it('renders the roster with leader tag and toggles own readiness', async () => {
    const party = new FakeChatConnection();
    party.setResponder(async (msgId, req) => {
      if (msgId === MsgID.SET_READY_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1', ready: true });
        return { code: 0 };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        applySnapshot(services.partyState, {
          partyId: 'party-1',
          leaderId: 'user_a',
          maxMembers: 5,
          members: [
            { userId: 'user_a', ready: false },
            { userId: 'user_b', ready: true },
          ],
        });
      },
    });

    expect(screen.getByText(`user_a (${zh.party.leaderTag})`)).toBeTruthy();
    expect(screen.getByText('user_b')).toBeTruthy();

    fireEvent.click(screen.getByTestId('party-ready-toggle'));
    await waitFor(() =>
      expect(party.requests.some((r) => r.msgId === MsgID.SET_READY_REQ)).toBe(true),
    );
  });

  it('gives the leader kick and transfer over other members behind confirms', async () => {
    const confirm = vi.spyOn(window, 'confirm').mockReturnValue(true);
    const party = new FakeChatConnection();
    party.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        applySnapshot(services.partyState, {
          partyId: 'party-1',
          leaderId: 'user_a',
          maxMembers: 5,
          members: [
            { userId: 'user_a', ready: true },
            { userId: 'user_b', ready: false },
          ],
        });
      },
    });

    fireEvent.click(screen.getByRole('button', { name: zh.party.kick }));
    await waitFor(() =>
      expect(party.requests.some((r) => r.msgId === MsgID.KICK_PARTY_MEMBER_REQ)).toBe(true),
    );
    expect(confirm).toHaveBeenCalledWith(zh.party.kickConfirm('user_b'));

    fireEvent.click(screen.getByRole('button', { name: zh.party.transferLeader }));
    await waitFor(() =>
      expect(party.requests.some((r) => r.msgId === MsgID.TRANSFER_LEADER_REQ)).toBe(true),
    );
    confirm.mockRestore();
  });

  it('sends invites and surfaces failures inline', async () => {
    const party = new FakeChatConnection();
    let fail = true;
    party.setResponder(async (msgId) => {
      if (msgId === MsgID.INVITE_TO_PARTY_REQ) {
        return { code: fail ? 4 : 0 };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        applySnapshot(services.partyState, {
          partyId: 'party-1',
          leaderId: 'user_a',
          maxMembers: 5,
          members: [{ userId: 'user_a', ready: false }],
        });
      },
    });

    const input = screen.getByTestId('party-invite-input').querySelector('input')!;
    fireEvent.change(input, { target: { value: 'user_c' } });
    fireEvent.click(screen.getByRole('button', { name: zh.party.invite }));
    await waitFor(() => expect(screen.getByText(zh.party.inviteFailed)).toBeTruthy());

    fail = false;
    fireEvent.change(input, { target: { value: 'user_c' } });
    fireEvent.click(screen.getByRole('button', { name: zh.party.invite }));
    await waitFor(() =>
      expect(party.requests.filter((r) => r.msgId === MsgID.INVITE_TO_PARTY_REQ)).toHaveLength(2),
    );
  });

  it('lists incoming invites and declining removes the row', async () => {
    const party = new FakeChatConnection();
    party.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        addInvite(services.partyState, 'inv-1', 'user_c', 'party-9');
      },
    });
    expect(screen.getByText(zh.party.inviteFrom('user_c'))).toBeTruthy();

    // The row has [accept, decline]; click decline.
    const acceptButton = screen.getByTestId('party-accept-user_c');
    const declineButton = acceptButton.parentElement!.querySelectorAll('button')[1];
    fireEvent.click(declineButton);
    await waitFor(() =>
      expect(
        party.requests.some(
          (r) =>
            r.msgId === MsgID.DECLINE_INVITE_REQ &&
            (r.req as { inviteId: string }).inviteId === 'inv-1',
        ),
      ).toBe(true),
    );
    await waitFor(() => expect(screen.queryByText(zh.party.inviteFrom('user_c'))).toBeNull());
  });

  it('leaves the party after a confirm', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    const party = new FakeChatConnection();
    party.setResponder(async (msgId, req) => {
      if (msgId === MsgID.LEAVE_PARTY_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1' });
        return { code: 0, partyDisbanded: false };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<PartyDialog open onClose={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        applySnapshot(services.partyState, {
          partyId: 'party-1',
          leaderId: 'user_b',
          maxMembers: 5,
          members: [
            { userId: 'user_a', ready: false },
            { userId: 'user_b', ready: false },
          ],
        });
      },
    });
    // Non-leader: no disband button anywhere.
    expect(screen.queryByRole('button', { name: zh.party.disband })).toBeNull();

    fireEvent.click(screen.getByTestId('party-leave'));
    await waitFor(() =>
      expect(party.requests.some((r) => r.msgId === MsgID.LEAVE_PARTY_REQ)).toBe(true),
    );
    await waitFor(() => expect(screen.getByText(zh.party.noParty)).toBeTruthy());
  });

  it('badges the entry button with the pending invite count', async () => {
    const party = new FakeChatConnection();
    party.setResponder(async () => ({ code: 0 }));
    await renderLoggedIn(<PartyButton onClick={vi.fn()} />, {
      partyConn: party,
      prepare: ({ services }) => {
        addInvite(services.partyState, 'inv-1', 'user_c', 'party-9');
        addInvite(services.partyState, 'inv-2', 'user_d', 'party-9');
      },
    });
    const badge = screen.getByTestId('party-invite-badge').querySelector('.MuiBadge-badge');
    expect(badge?.textContent).toBe('2');
  });
});
