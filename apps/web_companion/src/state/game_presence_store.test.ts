import { describe, expect, it } from 'vitest';
import {
  applyGamePresence,
  createGamePresenceStore,
  setGamePresenceEnabled,
  setGamePresenceUnavailable,
} from './game_presence_store';

describe('game presence store', () => {
  it('starts at the default-enabled, not-yet-loaded shape', () => {
    const presence = createGamePresenceStore();
    // 缺省语义:从未设置过的账号 = 开启(绑定即默认开启)。
    expect(presence.get().enabled).toBe(true);
    expect(presence.get().loaded).toBe(false);
    expect(presence.get().games).toEqual([]);
  });

  it('applyGamePresence stores the authoritative snapshot and marks loaded', () => {
    const presence = createGamePresenceStore();
    applyGamePresence(presence, { enabled: false, games: [] });
    expect(presence.get()).toMatchObject({ enabled: false, loaded: true, unavailable: false });
    applyGamePresence(presence, { enabled: true, games: ['game_a', 'game_b'] });
    expect(presence.get().games).toEqual(['game_a', 'game_b']);
  });

  it('switching off drops the published games locally; switching on keeps them', () => {
    const presence = createGamePresenceStore();
    applyGamePresence(presence, { enabled: true, games: ['game_a'] });
    setGamePresenceEnabled(presence, false);
    expect(presence.get()).toMatchObject({ enabled: false, games: [] });
    setGamePresenceEnabled(presence, true);
    expect(presence.get().enabled).toBe(true);
  });

  it('going unavailable forgets the mirror; refresh brings it back', () => {
    const presence = createGamePresenceStore();
    applyGamePresence(presence, { enabled: true, games: ['game_a'] });
    setGamePresenceUnavailable(presence, true);
    expect(presence.get()).toMatchObject({ unavailable: true, loaded: false, games: [] });
    applyGamePresence(presence, { enabled: true, games: ['game_a'] });
    expect(presence.get().unavailable).toBe(false);
  });
});
