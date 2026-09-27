import { describe, expect, it } from 'vitest';
import { MsgID } from '@chirp/proto/gateway';
import { GamePresenceApi } from './game_presence_api';
import { createStore } from '../state/store';
import { createGamePresenceStore } from '../state/game_presence_store';
import type { AuthState } from '../state/auth_store';
import { FakeChatConnection } from '../state/test_helpers';

const makeHarness = (loggedIn = true) => {
  const conn = new FakeChatConnection();
  const auth = createStore<AuthState>({
    userId: 'user_a',
    kicked: false,
    deviceId: 'browser-1',
    loggedIn,
  });
  const presence = createGamePresenceStore();
  const api = new GamePresenceApi({ conn, auth, presence });
  return { conn, auth, presence, api };
};

describe('GamePresenceApi.refresh', () => {
  it('pulls switch + games and marks the store loaded', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      expect(msgId).toBe(MsgID.GET_GAME_PRESENCE_REQ);
      return {
        code: 0,
        enabled: true,
        entries: [{ gameId: 'game_a', gameUserId: 'ga1' }],
      };
    });
    expect(await h.api.refresh()).toBe(true);
    // player_id 由 edge 钉死,客户端只发空串。
    expect(h.conn.requests[0].req).toMatchObject({ playerId: '' });
    expect(h.presence.get()).toMatchObject({ enabled: true, loaded: true, games: ['game_a'] });
  });

  it('a disabled account comes back with no games', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => ({ code: 0, enabled: false, entries: [] }));
    await h.api.refresh();
    expect(h.presence.get()).toMatchObject({ enabled: false, games: [] });
  });

  it('marks the plane unavailable when the edge errors, chat unaffected', async () => {
    const h = makeHarness();
    h.conn.failNextRequest();
    expect(await h.api.refresh()).toBe(false);
    expect(h.presence.get().unavailable).toBe(true);
    expect(h.presence.get().loaded).toBe(false);
  });

  it('stays silent while logged out', async () => {
    const h = makeHarness(false);
    expect(await h.api.refresh()).toBe(false);
    expect(h.conn.requests).toHaveLength(0);
  });
});

describe('GamePresenceApi.setEnabled', () => {
  it('writes the switch then refreshes to the authoritative list', async () => {
    const h = makeHarness();
    const seen: number[] = [];
    h.conn.setResponder(async (msgId) => {
      seen.push(msgId);
      if (msgId === MsgID.SET_GAME_PRESENCE_ENABLED_REQ) return { code: 0 };
      return { code: 0, enabled: false, entries: [] };
    });
    expect(await h.api.setEnabled(false)).toBe(true);
    // 写成功后立即刷新:关闭会让服务端清掉 roster,游戏清单必须跟着清空。
    expect(seen).toEqual([MsgID.SET_GAME_PRESENCE_ENABLED_REQ, MsgID.GET_GAME_PRESENCE_REQ]);
    expect(h.conn.requests[0].req).toMatchObject({ playerId: '', enabled: false });
    expect(h.presence.get()).toMatchObject({ enabled: false, games: [], loaded: true });
  });

  it('a rejected write leaves the mirror untouched', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => ({ code: 0, enabled: true, entries: [] }));
    await h.api.refresh(); // prime: GET answers enabled with no games yet
    h.conn.requests.length = 0;
    h.conn.setResponder(async (msgId) =>
      msgId === MsgID.SET_GAME_PRESENCE_ENABLED_REQ ? { code: 5 } : { code: 0, enabled: true, entries: [] },
    );
    expect(await h.api.setEnabled(false)).toBe(false);
    expect(h.conn.requests).toHaveLength(1); // no refresh after a failed write
    expect(h.presence.get()).toMatchObject({ enabled: true, loaded: true });
  });
});
