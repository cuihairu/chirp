import { describe, expect, it } from 'vitest';
import { clearTyping, createTypingStore, setTyping, typingUsersOf } from './typing_store';

describe('typing_store', () => {
  it('tracks the last typing timestamp per channel per user', () => {
    const store = createTypingStore();
    setTyping(store, 'p:a|b', 'user_b', 1000);
    setTyping(store, 'p:a|b', 'user_c', 2000);
    setTyping(store, 'g:g1', 'user_b', 3000);
    expect(store.get().byChannel['p:a|b']).toEqual({ user_b: 1000, user_c: 2000 });
    expect(store.get().byChannel['g:g1']).toEqual({ user_b: 3000 });
  });

  it('refreshing a user keeps a single entry', () => {
    const store = createTypingStore();
    setTyping(store, 'p:a|b', 'user_b', 1000);
    setTyping(store, 'p:a|b', 'user_b', 5000);
    expect(store.get().byChannel['p:a|b']).toEqual({ user_b: 5000 });
  });

  it('drops expired entries from typingUsersOf but keeps them in state', () => {
    const store = createTypingStore();
    setTyping(store, 'p:a|b', 'user_b', 1000);
    setTyping(store, 'p:a|b', 'user_c', 6000);
    // TTL is 6s: at t=7100 user_b (at 1000) expired, user_c is still fresh.
    expect(typingUsersOf(store.get(), 'p:a|b', 7100)).toEqual(['user_c']);
    expect(typingUsersOf(store.get(), 'p:a|b', 6500)).toEqual(['user_b', 'user_c']);
    expect(typingUsersOf(store.get(), 'g:other', 6500)).toEqual([]);
  });

  it('clearTyping removes one user and leaves others untouched', () => {
    const store = createTypingStore();
    setTyping(store, 'p:a|b', 'user_b', 1000);
    setTyping(store, 'p:a|b', 'user_c', 2000);
    clearTyping(store, 'p:a|b', 'user_b');
    expect(store.get().byChannel['p:a|b']).toEqual({ user_c: 2000 });
    // Clearing an unknown user is a no-op (same state reference).
    const before = store.get();
    clearTyping(store, 'p:a|b', 'ghost');
    expect(store.get()).toBe(before);
  });
});
