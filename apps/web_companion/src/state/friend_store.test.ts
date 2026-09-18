import { describe, expect, it } from 'vitest';
import {
  addFriend,
  addPendingIn,
  addPendingOut,
  createFriendStore,
  fromUserIdOf,
  removeFriend,
  replaceFriends,
  replacePendingIn,
  resolvePending,
} from './friend_store';

describe('friend_store', () => {
  it('tracks friends, incoming and outgoing requests without duplicates', () => {
    const store = createFriendStore();
    addFriend(store, 'user_c');
    addFriend(store, 'user_a');
    addFriend(store, 'user_a'); // duplicate is a no-op
    expect(store.get().friends).toEqual(['user_a', 'user_c']);
    addPendingIn(store, 'req-1', 'user_b');
    addPendingIn(store, 'req-1', 'user_b'); // same request id: no dup
    expect(store.get().pendingIn).toEqual([{ requestId: 'req-1', fromUserId: 'user_b' }]);
    addPendingOut(store, 'user_d');
    addPendingOut(store, 'user_d');
    expect(store.get().pendingOut).toEqual(['user_d']);

    // Resolving a request keeps everything else intact.
    resolvePending(store, 'req-1');
    expect(store.get().pendingIn).toEqual([]);

    // Re-adding an existing friend keeps the state reference (no republish).
    const before = store.get();
    addFriend(store, 'user_a');
    expect(store.get()).toBe(before);

    removeFriend(store, 'user_a');
    expect(store.get().friends).toEqual(['user_c']);
  });

  it('looks up the sender of a pending request', () => {
    const store = createFriendStore();
    addPendingIn(store, 'req-9', 'user_b');
    expect(fromUserIdOf(store.get(), 'req-9')).toBe('user_b');
    expect(fromUserIdOf(store.get(), 'missing')).toBeUndefined();
  });

  it('replaces friends and pending requests wholesale (server pulls)', () => {
    const store = createFriendStore();
    replaceFriends(store, ['user_z', 'user_a']);
    expect(store.get().friends).toEqual(['user_a', 'user_z']);
    replacePendingIn(store, [
      { requestId: 'r1', fromUserId: 'user_b' },
      { requestId: 'r2', fromUserId: 'user_c' },
    ]);
    expect(store.get().pendingIn).toEqual([
      { requestId: 'r1', fromUserId: 'user_b' },
      { requestId: 'r2', fromUserId: 'user_c' },
    ]);

    // An identical pull keeps the state reference (no republish).
    const before = store.get();
    replaceFriends(store, ['user_a', 'user_z']);
    expect(store.get()).toBe(before);
  });
});
