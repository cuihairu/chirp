import { describe, expect, it, vi } from 'vitest';
import {
  addInvite,
  applySnapshot,
  clearParty,
  createPartyStore,
  isLeaderOf,
  removeInvite,
  resetInvites,
  selfMemberOf,
  type PartySnapshot,
} from './party_store';

const snapshot = (overrides: Partial<PartySnapshot> = {}): PartySnapshot => ({
  partyId: 'party-1',
  leaderId: 'user_a',
  maxMembers: 5,
  members: [
    { userId: 'user_a', ready: true },
    { userId: 'user_b', ready: false },
  ],
  ...overrides,
});

describe('party store', () => {
  it('applies full snapshots and never diffs events', () => {
    const party = createPartyStore();
    applySnapshot(party, snapshot());
    expect(party.get().party).toEqual(snapshot());

    // A later snapshot replaces the whole membership, member order included.
    applySnapshot(party, snapshot({ leaderId: 'user_b', members: [{ userId: 'user_b', ready: false }] }));
    expect(party.get().party?.members).toEqual([{ userId: 'user_b', ready: false }]);
  });

  it('clears the party only (invites survive) and is idempotent', () => {
    const party = createPartyStore();
    applySnapshot(party, snapshot());
    addInvite(party, 'inv-1', 'user_c', 'party-9');
    const listener = vi.fn();
    party.subscribe(listener);

    clearParty(party);
    expect(party.get().party).toBeNull();
    expect(party.get().invites).toEqual([expect.objectContaining({ inviteId: 'inv-1' })]);

    clearParty(party);
    expect(listener).toHaveBeenCalledTimes(1); // no-op clear does not notify
  });

  it('dedupes invites by id and removes them by id', () => {
    const party = createPartyStore();
    addInvite(party, 'inv-1', 'user_b', 'party-1');
    addInvite(party, 'inv-1', 'user_b', 'party-1'); // duplicate is a no-op
    addInvite(party, 'inv-2', 'user_c', 'party-2');
    expect(party.get().invites).toHaveLength(2);

    removeInvite(party, 'inv-1');
    expect(party.get().invites.map((i) => i.inviteId)).toEqual(['inv-2']);
  });

  it('resets invites on relogin without touching the snapshot', () => {
    const party = createPartyStore();
    applySnapshot(party, snapshot());
    addInvite(party, 'inv-1', 'user_b', 'party-1');

    resetInvites(party);
    expect(party.get().invites).toEqual([]);
    expect(party.get().party).toEqual(snapshot());

    resetInvites(party); // empty already: no-op
    expect(party.get().invites).toEqual([]);
  });

  it('answers leader and self-row queries from the held snapshot', () => {
    const party = createPartyStore();
    expect(isLeaderOf(party.get(), 'user_a')).toBe(false); // no snapshot yet
    expect(selfMemberOf(party.get(), 'user_a')).toBeUndefined();

    applySnapshot(party, snapshot());
    expect(isLeaderOf(party.get(), 'user_a')).toBe(true);
    expect(isLeaderOf(party.get(), 'user_b')).toBe(false);
    expect(selfMemberOf(party.get(), 'user_b')).toEqual({ userId: 'user_b', ready: false });
    expect(selfMemberOf(party.get(), 'user_z')).toBeUndefined();
  });
});
