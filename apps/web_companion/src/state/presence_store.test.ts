import { describe, expect, it } from 'vitest';
import { PresenceStatus } from '@chirp/proto/social';
import {
  createPresenceStore,
  presenceFresh,
  presenceOf,
  setPresence,
} from './presence_store';

describe('presence_store', () => {
  it('keeps the latest snapshot per user', () => {
    const store = createPresenceStore();
    setPresence(store, 'user_b', PresenceStatus.ONLINE, '', 1000);
    setPresence(store, 'user_c', PresenceStatus.OFFLINE, 'brb', 2000);
    setPresence(store, 'user_b', PresenceStatus.AWAY, 'coffee', 3000);
    expect(presenceOf(store.get(), 'user_b')).toEqual({
      status: PresenceStatus.AWAY,
      statusMessage: 'coffee',
      at: 3000,
    });
    expect(presenceOf(store.get(), 'ghost')).toBeUndefined();
  });

  it('ages entries out via presenceFresh', () => {
    const store = createPresenceStore();
    setPresence(store, 'user_b', PresenceStatus.ONLINE, '', 1000);
    expect(presenceFresh(store.get(), 'user_b', 1000 + 69_999)).toBe(true);
    expect(presenceFresh(store.get(), 'user_b', 1000 + 70_001)).toBe(false);
    expect(presenceFresh(store.get(), 'ghost', 2000)).toBe(false);
  });
});
