import { describe, expect, it } from 'vitest';
import { MsgID } from '@chirp/proto/gateway';
import {
  InviteNotify,
  PartyDisbandedNotify,
  PartyInfo,
  PartyKickedNotify,
  PartyStateChangedNotify,
} from '@chirp/proto/party';
import { PartyApi } from './party_api';
import { createStore } from '../state/store';
import { createPartyStore } from '../state/party_store';
import type { AuthState } from '../state/auth_store';
import { FakeChatConnection } from '../state/test_helpers';

const partyInfo = (overrides: Partial<{ partyId: string; leaderId: string }> = {}) =>
  PartyInfo.fromPartial({
    partyId: 'party-1',
    leaderId: 'user_a',
    maxMembers: 5,
    members: [
      { userId: 'user_a', ready: false },
      { userId: 'user_b', ready: true },
    ],
    ...overrides,
  });

const makeHarness = () => {
  const conn = new FakeChatConnection();
  const auth = createStore<AuthState>({
    userId: 'user_a',
    kicked: false,
    deviceId: 'dev-1',
    loggedIn: true,
  });
  const party = createPartyStore();
  const api = new PartyApi({ conn, auth, party });
  // Default green responder that reports "not in a party"; cases override.
  conn.setResponder(async () => ({ code: 0 }));
  return { conn, auth, party, api };
};

describe('PartyApi.login', () => {
  it('logs in, restores membership from GET_MY_PARTY, and clears stale invites', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_MY_PARTY_REQ) {
        return { code: 0, inParty: true, party: partyInfo() };
      }
      return { code: 0 };
    });
    expect(await h.api.login('user_a')).toBe(true);
    expect(h.party.get().party?.partyId).toBe('party-1');
    expect(h.party.get().party?.members).toEqual([
      { userId: 'user_a', ready: false },
      { userId: 'user_b', ready: true },
    ]);
  });

  it('reports no party after a clean not-in-party pull', async () => {
    const h = makeHarness();
    expect(await h.api.login('user_a')).toBe(true);
    expect(h.party.get().party).toBeNull();
  });

  it('disconnects and degrades on a login rejection', async () => {
    const h = makeHarness();
    let disconnected = false;
    h.conn.setResponder(async () => ({ code: 2 }));
    h.conn.disconnect = () => {
      disconnected = true;
    };
    expect(await h.api.login('user_a')).toBe(false);
    expect(disconnected).toBe(true);
  });
});

describe('PartyApi membership actions', () => {
  const loginInParty = async (h: ReturnType<typeof makeHarness>): Promise<void> => {
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_MY_PARTY_REQ) {
        return { code: 0, inParty: true, party: partyInfo() };
      }
      return { code: 0 };
    });
    await h.api.login('user_a');
  };

  it('creates a party and adopts the returned snapshot', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.CREATE_PARTY_REQ);
      expect(req).toMatchObject({ userId: 'user_a', maxMembers: 3 });
      return { code: 0, party: partyInfo({ partyId: 'party-new' }) };
    });
    expect(await h.api.createParty(3)).toBe(0);
    expect(h.party.get().party?.partyId).toBe('party-new');
  });

  it('invites need a party to invite from', async () => {
    const h = makeHarness();
    expect(await h.api.invite('user_b')).toBe(-1);
    expect(h.conn.requests.some((r) => r.msgId === MsgID.INVITE_TO_PARTY_REQ)).toBe(false);

    await loginInParty(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.INVITE_TO_PARTY_REQ);
      expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1', targetUserId: 'user_b' });
      return { code: 0 };
    });
    expect(await h.api.invite('user_b')).toBe(0);
  });

  it('accepting removes the invite and adopts the response snapshot', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.INVITE_NOTIFY,
      InviteNotify.encode(
        InviteNotify.fromPartial({ inviteId: 'inv-1', fromUserId: 'user_c' }),
      ).finish(),
    );
    expect(h.party.get().invites).toEqual([{ inviteId: 'inv-1', fromUserId: 'user_c', partyId: '' }]);

    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.ACCEPT_INVITE_REQ);
      expect(req).toMatchObject({ userId: 'user_a', inviteId: 'inv-1' });
      return { code: 0, party: partyInfo() };
    });
    expect(await h.api.acceptInvite('inv-1')).toBe(0);
    expect(h.party.get().invites).toEqual([]);
    expect(h.party.get().party?.partyId).toBe('party-1');
  });

  it('declining removes the invite without joining anything', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.INVITE_NOTIFY,
      InviteNotify.encode(
        InviteNotify.fromPartial({ inviteId: 'inv-2', fromUserId: 'user_c' }),
      ).finish(),
    );
    h.conn.setResponder(async (msgId) => {
      expect(msgId).toBe(MsgID.DECLINE_INVITE_REQ);
      return { code: 0 };
    });
    expect(await h.api.declineInvite('inv-2')).toBe(0);
    expect(h.party.get().invites).toEqual([]);
    expect(h.party.get().party).toBeNull();
  });

  it('leaving clears the mirror and reports whether we disbanded it', async () => {
    const h = makeHarness();
    await loginInParty(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.LEAVE_PARTY_REQ);
      expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1' });
      return { code: 0, partyDisbanded: true };
    });
    expect(await h.api.leaveParty()).toBe(true);
    expect(h.party.get().party).toBeNull();

    // Not in a party: leaving is a silent no-op.
    expect(await h.api.leaveParty()).toBe(false);
  });

  it('guards leader actions behind a held membership and forwards payloads', async () => {
    const h = makeHarness();
    expect(await h.api.kickMember('user_b')).toBe(-1);
    expect(await h.api.transferLeader('user_b')).toBe(-1);
    expect(await h.api.setReady(true)).toBe(-1);
    expect(await h.api.disbandParty()).toBe(-1);
    expect(h.conn.requests).toHaveLength(0); // nothing hit the wire

    await loginInParty(h);
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.KICK_PARTY_MEMBER_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1', targetUserId: 'user_b' });
      }
      if (msgId === MsgID.TRANSFER_LEADER_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1', targetUserId: 'user_b' });
      }
      if (msgId === MsgID.SET_READY_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1', ready: true });
      }
      if (msgId === MsgID.DISBAND_PARTY_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', partyId: 'party-1' });
      }
      return { code: 0 };
    });
    expect(await h.api.kickMember('user_b')).toBe(0);
    expect(await h.api.transferLeader('user_b')).toBe(0);
    expect(await h.api.setReady(true)).toBe(0);
    expect(await h.api.disbandParty()).toBe(0);
  });
});

describe('PartyApi notifies', () => {
  it('applies PARTY_STATE_CHANGED as a full snapshot', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.PARTY_STATE_CHANGED_NOTIFY,
      PartyStateChangedNotify.encode(
        PartyStateChangedNotify.fromPartial({ party: partyInfo({ leaderId: 'user_b' }) }),
      ).finish(),
    );
    expect(h.party.get().party?.leaderId).toBe('user_b');
  });

  it('clears the mirror on kicked and disbanded notifies', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_MY_PARTY_REQ) {
        return { code: 0, inParty: true, party: partyInfo() };
      }
      return { code: 0 };
    });
    await h.api.login('user_a');
    expect(h.party.get().party).not.toBeNull();

    h.conn.emit(
      MsgID.PARTY_KICKED_NOTIFY,
      PartyKickedNotify.encode(PartyKickedNotify.fromPartial({ partyId: 'party-1' })).finish(),
    );
    expect(h.party.get().party).toBeNull();

    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_MY_PARTY_REQ) {
        return { code: 0, inParty: true, party: partyInfo() };
      }
      return { code: 0 };
    });
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.PARTY_DISBANDED_NOTIFY,
      PartyDisbandedNotify.encode(PartyDisbandedNotify.fromPartial({ partyId: 'party-1' })).finish(),
    );
    expect(h.party.get().party).toBeNull();
  });

  it('ignores malformed notify bodies instead of throwing', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    expect(() =>
      h.conn.emit(MsgID.PARTY_STATE_CHANGED_NOTIFY, new Uint8Array([0x01, 0xff, 0x00])),
    ).not.toThrow();
    expect(h.party.get().party).toBeNull();
  });

  it('stops handling notifies after logout', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.api.logout();
    h.conn.emit(
      MsgID.INVITE_NOTIFY,
      InviteNotify.encode(
        InviteNotify.fromPartial({ inviteId: 'inv-3', fromUserId: 'user_c' }),
      ).finish(),
    );
    expect(h.party.get().invites).toEqual([]);
  });
});
