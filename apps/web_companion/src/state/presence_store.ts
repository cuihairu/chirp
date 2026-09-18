import { createStore, type Store } from './store';
import { PresenceStatus } from '@chirp/proto/social';

/**
 * Peer presence, keyed by user id. Entries are advisory snapshots: the
 * social service broadcasts PRESENCE_NOTIFY to friends, and the client
 * bulk-pulls GET_PRESENCE when the conversation list changes. There is no
 * server-side timeout guarantee, so stale entries age out in the UI (see
 * presenceFresh).
 */
export interface PresenceState {
  byUser: Record<string, { status: PresenceStatus; statusMessage: string; at: number }>;
}

export const createPresenceStore = (): Store<PresenceState> =>
  createStore<PresenceState>({ byUser: {} });

export function setPresence(
  store: Store<PresenceState>,
  userId: string,
  status: PresenceStatus,
  statusMessage: string,
  at: number,
): void {
  store.set((prev) => ({
    byUser: { ...prev.byUser, [userId]: { status, statusMessage, at } },
  }));
}

export const presenceOf = (state: PresenceState, userId: string) => state.byUser[userId];

/** Presences older than this render as offline (missed disconnect notifies). */
export const PRESENCE_TTL_MS = 70_000;

export const presenceFresh = (state: PresenceState, userId: string, now: number): boolean => {
  const entry = state.byUser[userId];
  return !!entry && now - entry.at < PRESENCE_TTL_MS;
};
