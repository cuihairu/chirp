import { createStore, type Store } from './store';

/**
 * The social service has no friend-listing API yet (GET_FRIEND_LIST 3007 is
 * unimplemented), so the client keeps its own roster: everyone we sent a
 * request to or accepted one from. Persisted per user in localStorage as a
 * stopgap — the authoritative list arrives with the P1 aggregation plane.
 */
export interface FriendState {
  /** Accepted friends (user ids, sorted). */
  friends: string[];
  /** Requests we received and have not answered. */
  pendingIn: Array<{ requestId: string; fromUserId: string }>;
  /** Requests we sent that nobody has answered yet. */
  pendingOut: string[];
}

export const createFriendStore = (initial: FriendState = { friends: [], pendingIn: [], pendingOut: [] }): Store<FriendState> =>
  createStore<FriendState>(initial);

export function addFriend(store: Store<FriendState>, userId: string): void {
  store.set((prev) =>
    prev.friends.includes(userId)
      ? prev
      : { ...prev, friends: [...prev.friends, userId].sort() },
  );
}

export function removeFriend(store: Store<FriendState>, userId: string): void {
  store.set((prev) =>
    prev.friends.includes(userId) ? { ...prev, friends: prev.friends.filter((f) => f !== userId) } : prev,
  );
}

export function addPendingOut(store: Store<FriendState>, userId: string): void {
  store.set((prev) =>
    prev.pendingOut.includes(userId) || prev.friends.includes(userId)
      ? prev
      : { ...prev, pendingOut: [...prev.pendingOut, userId].sort() },
  );
}

export function addPendingIn(store: Store<FriendState>, requestId: string, fromUserId: string): void {
  store.set((prev) =>
    prev.pendingIn.some((r) => r.requestId === requestId)
      ? prev
      : { ...prev, pendingIn: [...prev.pendingIn, { requestId, fromUserId }] },
  );
}

export function resolvePending(store: Store<FriendState>, requestId: string): void {
  store.set((prev) => ({
    ...prev,
    pendingIn: prev.pendingIn.filter((r) => r.requestId !== requestId),
  }));
}

export const fromUserIdOf = (
  state: FriendState,
  requestId: string,
): string | undefined => state.pendingIn.find((r) => r.requestId === requestId)?.fromUserId;

const friendStorageKey = (userId: string): string => `chirp.friends.${userId}`;

/** Best-effort load; storage may be unavailable (private mode) or corrupt. */
export function loadFriendState(userId: string): FriendState {
  const empty: FriendState = { friends: [], pendingIn: [], pendingOut: [] };
  try {
    const raw = window.localStorage.getItem(friendStorageKey(userId));
    if (!raw) return empty;
    const parsed = JSON.parse(raw) as Partial<FriendState>;
    return {
      friends: parsed.friends ?? [],
      pendingIn: parsed.pendingIn ?? [],
      pendingOut: parsed.pendingOut ?? [],
    };
  } catch {
    return empty;
  }
}

export function saveFriendState(userId: string, state: FriendState): void {
  try {
    window.localStorage.setItem(friendStorageKey(userId), JSON.stringify(state));
  } catch {
    // Non-fatal: the roster is a local stopgap anyway.
  }
}

/** Subscribe so every mutation lands in localStorage (fire and forget). */
export function persistFriendStore(store: Store<FriendState>, userId: string): () => void {
  const save = (): void => saveFriendState(userId, store.get());
  save();
  return store.subscribe(save);
}
