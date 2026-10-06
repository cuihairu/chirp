import { createStore, type Store } from './store';

/**
 * Client-side mirror of the social plane's roster. The backend is
 * authoritative: login pulls the friend list and the incoming request queue
 * (GET_FRIEND_LIST / GET_PENDING_REQUESTS), and notifications keep the mirror
 * current. Only pendingOut lives purely client-side — the service has no
 * outgoing-request query yet, so a page refresh drops that list.
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

export function replaceFriends(store: Store<FriendState>, ids: string[]): void {
  const sorted = [...ids].sort();
  store.set((prev) =>
    prev.friends.length === sorted.length && prev.friends.every((f, i) => f === sorted[i])
      ? prev
      : { ...prev, friends: sorted },
  );
}

export function replacePendingIn(
  store: Store<FriendState>,
  requests: Array<{ requestId: string; fromUserId: string }>,
): void {
  store.set((prev) => ({ ...prev, pendingIn: requests }));
}

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
