import { createStore, type Store } from './store';

/**
 * Client-side mirror of the party plane. The backend is authoritative and
 * speaks in full snapshots: PARTY_STATE_CHANGED carries the whole PartyInfo
 * to every member (including the actor), so the mirror only ever applies
 * snapshots and clears itself on kick/disband. Invites mirror the incoming
 * queue; the service keeps no outgoing-invite query, so invites we sent are
 * not tracked (the inviter learns the outcome via INVITE_RESULT_NOTIFY).
 */
export interface PartyState {
  /** Current membership snapshot; null when not in a party. */
  party: PartySnapshot | null;
  /** Invites we received and have not answered. */
  invites: Array<{ inviteId: string; fromUserId: string; partyId: string }>;
}

/** Narrowed copy of chirp.party.PartyInfo the UI renders. */
export interface PartySnapshot {
  partyId: string;
  leaderId: string;
  maxMembers: number;
  members: Array<{ userId: string; ready: boolean }>;
}

export const createPartyStore = (
  initial: PartyState = { party: null, invites: [] },
): Store<PartyState> => createStore<PartyState>(initial);

/** Applies a full server snapshot (PARTY_STATE_CHANGED / join response). */
export function applySnapshot(store: Store<PartyState>, snapshot: PartySnapshot): void {
  store.set((prev) => ({ ...prev, party: snapshot }));
}

/** Leaving / being kicked / disbanding all land here. */
export function clearParty(store: Store<PartyState>): void {
  store.set((prev) => (prev.party === null ? prev : { ...prev, party: null }));
}

export function addInvite(
  store: Store<PartyState>,
  inviteId: string,
  fromUserId: string,
  partyId: string,
): void {
  store.set((prev) =>
    prev.invites.some((i) => i.inviteId === inviteId)
      ? prev
      : { ...prev, invites: [...prev.invites, { inviteId, fromUserId, partyId }] },
  );
}

export function removeInvite(store: Store<PartyState>, inviteId: string): void {
  store.set((prev) => ({
    ...prev,
    invites: prev.invites.filter((i) => i.inviteId !== inviteId),
  }));
}

/** Login/relogin must not leak the previous account's invites. */
export function resetInvites(store: Store<PartyState>): void {
  store.set((prev) => (prev.invites.length === 0 ? prev : { ...prev, invites: [] }));
}

/** True when userId is the leader of the snapshot we hold. */
export const isLeaderOf = (state: PartyState, userId: string): boolean =>
  state.party?.leaderId === userId;

/** Our own membership row, if we hold a snapshot. */
export const selfMemberOf = (
  state: PartyState,
  userId: string,
): { userId: string; ready: boolean } | undefined =>
  state.party?.members.find((m) => m.userId === userId);
