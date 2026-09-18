import { describe, expect, it, vi } from 'vitest';
import {
  addFriend,
  addPendingIn,
  addPendingOut,
  createFriendStore,
  fromUserIdOf,
  loadFriendState,
  persistFriendStore,
  removeFriend,
  resolvePending,
  saveFriendState,
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

  it('persists to localStorage and loads back', () => {
    saveFriendState('user_a', { friends: ['user_b'], pendingIn: [], pendingOut: ['user_c'] });
    expect(loadFriendState('user_a').friends).toEqual(['user_b']);

    // Mutating a persisted store writes through.
    const store = createFriendStore(loadFriendState('user_a'));
    const off = persistFriendStore(store, 'user_a');
    addFriend(store, 'user_d');
    expect(loadFriendState('user_a').friends).toEqual(['user_b', 'user_d']);
    off();

    // Corrupt or missing storage falls back to an empty roster.
    window.localStorage.setItem('chirp.friends.user_e', 'not json');
    expect(loadFriendState('user_e')).toEqual({ friends: [], pendingIn: [], pendingOut: [] });
    expect(loadFriendState('nobody')).toEqual({ friends: [], pendingIn: [], pendingOut: [] });
  });

  it('silently ignores persistence failures', () => {
    const store = createFriendStore();
    const setItem = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('quota');
    });
    expect(() => persistFriendStore(store, 'user_a')).not.toThrow();
    setItem.mockRestore();
  });
});
