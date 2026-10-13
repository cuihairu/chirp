import { describe, expect, it } from 'vitest';
import { PresenceStatus } from '@chirp/proto/social';
import {
  FriendAcceptedNotify,
  FriendRequestNotify,
  FriendRemovedNotify,
  PresenceNotify,
} from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';
import { SocialApi } from './social_api';
import { createStore } from '../state/store';
import { createPresenceStore, presenceOf } from '../state/presence_store';
import { createFriendStore } from '../state/friend_store';
import type { AuthState } from '../state/auth_store';
import { FakeChatConnection } from '../state/test_helpers';

const makeHarness = () => {
  const conn = new FakeChatConnection();
  const auth = createStore<AuthState>({
    userId: 'user_a',
    kicked: false,
    deviceId: 'dev-1',
    loggedIn: true,
  });
  const presence = createPresenceStore();
  const friends = createFriendStore();
  const api = new SocialApi({ conn, auth, presence, friends });
  // Default green responder; individual cases override it.
  conn.setResponder(async () => ({ code: 0 }));
  return { conn, auth, presence, friends, api };
};

describe('SocialApi.login', () => {
  it('logs in with the scaffold token and reports success', async () => {
    const h = makeHarness();
    let sawPlatform: unknown;
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.LOGIN_REQ) {
        sawPlatform = (req as { platform?: string }).platform;
      }
      return { code: 0 };
    });
    expect(await h.api.login('user_a')).toBe(true);
    expect(sawPlatform).toBe('web');
  });

  it('pulls the authoritative roster and pending queue on login', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_FRIEND_LIST_REQ) {
        return {
          code: 0,
          friends: [
            { userId: 'user_b', status: 2 },
            { userId: 'user_c', status: 2 },
          ],
          totalCount: 2,
        };
      }
      if (msgId === MsgID.GET_PENDING_REQUESTS_REQ) {
        return { code: 0, requests: [{ requestId: 'req-7', fromUserId: 'user_d' }] };
      }
      return { code: 0 };
    });
    await h.api.login('user_a');
    expect(h.friends.get().friends).toEqual(['user_b', 'user_c']);
    expect(h.friends.get().pendingIn).toEqual([{ requestId: 'req-7', fromUserId: 'user_d' }]);
  });

  it('degrades quietly on a social failure', async () => {
    const h = makeHarness();
    let disconnected = false;
    h.conn.setResponder(async () => ({ code: 2 }));
    h.conn.disconnect = () => {
      disconnected = true;
    };
    expect(await h.api.login('user_a')).toBe(false);
    expect(disconnected).toBe(true);
  });
});

describe('SocialApi presence', () => {
  it('advertises own status and pulls peer snapshots', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.SET_PRESENCE_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', status: PresenceStatus.ONLINE });
        return { code: 0 };
      }
      if (msgId === MsgID.GET_PRESENCE_REQ) {
        expect((req as { userIds: string[] }).userIds).toEqual(['user_b', 'user_c']);
        return {
          code: 0,
          presences: [
            { userId: 'user_b', status: PresenceStatus.ONLINE, statusMessage: 'hi' },
            { userId: 'user_c', status: PresenceStatus.OFFLINE, statusMessage: '' },
          ],
        };
      }
      return { code: 0 };
    });
    await h.api.setPresence(PresenceStatus.ONLINE);
    await h.api.pullPresence(['user_b', 'user_c']);
    expect(presenceOf(h.presence.get(), 'user_b')).toMatchObject({
      status: PresenceStatus.ONLINE,
      statusMessage: 'hi',
    });
    expect(presenceOf(h.presence.get(), 'user_c')?.status).toBe(PresenceStatus.OFFLINE);
  });

  it('applies PRESENCE_NOTIFY snapshots', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.PRESENCE_NOTIFY,
      PresenceNotify.encode(
        PresenceNotify.fromPartial({
          userId: 'user_b',
          status: PresenceStatus.ONLINE,
          statusMessage: 'playing',
        }),
      ).finish(),
    );
    expect(presenceOf(h.presence.get(), 'user_b')).toMatchObject({
      status: PresenceStatus.ONLINE,
      statusMessage: 'playing',
    });
  });
});

describe('SocialApi friends', () => {
  it('records outgoing requests on success only', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.ADD_FRIEND_REQ);
      expect(req).toMatchObject({ userId: 'user_a', targetUserId: 'user_b' });
      return { code: 0 };
    });
    expect(await h.api.addFriend('user_b')).toBe(0);
    expect(h.friends.get().pendingOut).toEqual(['user_b']);

    h.conn.setResponder(async () => ({ code: 8 }));
    expect(await h.api.addFriend('user_c')).toBe(8);
    expect(h.friends.get().pendingOut).toEqual(['user_b']);
  });

  it('accepting clears the request; the roster lands via the ACCEPTED notify', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.FRIEND_REQUEST_NOTIFY,
      FriendRequestNotify.encode(
        FriendRequestNotify.fromPartial({ requestId: 'req-1', fromUserId: 'user_b' }),
      ).finish(),
    );
    expect(h.friends.get().pendingIn).toEqual([{ requestId: 'req-1', fromUserId: 'user_b' }]);

    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.FRIEND_REQUEST_ACTION_REQ);
      expect(req).toMatchObject({ userId: 'user_a', requestId: 'req-1', accept: true });
      return { code: 0 };
    });
    expect(await h.api.respondRequest('req-1', true)).toBe(0);
    expect(h.friends.get().pendingIn).toEqual([]);
    // No local bookkeeping anymore: the friendship is booked when the
    // server's ACCEPTED notify (user_id = the other party) arrives.
    expect(h.friends.get().friends).toEqual([]);
    h.conn.emit(
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      FriendAcceptedNotify.encode(
        FriendAcceptedNotify.fromPartial({ userId: 'user_b' }),
      ).finish(),
    );
    expect(h.friends.get().friends).toEqual(['user_b']);
  });

  it('declining a request never books a friendship', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.FRIEND_REQUEST_NOTIFY,
      FriendRequestNotify.encode(
        FriendRequestNotify.fromPartial({ requestId: 'req-2', fromUserId: 'user_c' }),
      ).finish(),
    );
    h.conn.setResponder(async () => ({ code: 0 }));
    await h.api.respondRequest('req-2', false);
    expect(h.friends.get().friends).toEqual([]);
  });

  it('never friends itself from the requester-shaped ACCEPTED notify', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    // Server quirk: user_id carries the requester (us) on this notify.
    h.conn.emit(
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      FriendAcceptedNotify.encode(
        FriendAcceptedNotify.fromPartial({ userId: 'user_a' }),
      ).finish(),
    );
    expect(h.friends.get().friends).toEqual([]);

    h.conn.emit(
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      FriendAcceptedNotify.encode(
        FriendAcceptedNotify.fromPartial({ userId: 'user_d' }),
      ).finish(),
    );
    expect(h.friends.get().friends).toEqual(['user_d']);
  });

  it('removes a friend through the server and mirrors it locally', async () => {
    const h = makeHarness();
    h.friends.set((prev) => ({ ...prev, friends: ['user_b'] }));
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.REMOVE_FRIEND_REQ);
      expect(req).toMatchObject({ userId: 'user_a', friendUserId: 'user_b' });
      return { code: 0 };
    });
    expect(await h.api.removeFriend('user_b')).toBe(0);
    expect(h.friends.get().friends).toEqual([]);
  });

  it('blocks through the server (severing the friendship) and unblocks', async () => {
    const h = makeHarness();
    h.friends.set((prev) => ({ ...prev, friends: ['user_b'] }));
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.BLOCK_USER_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', targetUserId: 'user_b' });
        return { code: 0 };
      }
      if (msgId === MsgID.UNBLOCK_USER_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', targetUserId: 'user_c' });
        return { code: 0 };
      }
      return { code: 0 };
    });
    expect(await h.api.blockUser('user_b')).toBe(0);
    expect(h.friends.get().friends).toEqual([]);
    expect(await h.api.unblockUser('user_c')).toBe(0);
  });

  it('drops a friend on FRIEND_REMOVED_NOTIFY', async () => {
    const h = makeHarness();
    h.friends.set((prev) => ({ ...prev, friends: ['user_b'] }));
    await h.api.login('user_a');
    h.conn.emit(
      MsgID.FRIEND_REMOVED_NOTIFY,
      FriendRemovedNotify.encode(FriendRemovedNotify.fromPartial({ userId: 'user_b' })).finish(),
    );
    expect(h.friends.get().friends).toEqual([]);
  });

  it('stops handling notifies after logout', async () => {
    const h = makeHarness();
    await h.api.login('user_a');
    h.api.logout();
    h.conn.emit(
      MsgID.PRESENCE_NOTIFY,
      PresenceNotify.encode(
        PresenceNotify.fromPartial({ userId: 'user_b', status: PresenceStatus.ONLINE }),
      ).finish(),
    );
    expect(presenceOf(h.presence.get(), 'user_b')).toBeUndefined();
  });
});

describe('SocialApi friend-online notify', () => {
  const frame = (partial: Partial<PresenceNotify>): Uint8Array =>
    PresenceNotify.encode(PresenceNotify.fromPartial(partial)).finish();

  it('fans FRIEND_ONLINE_NOTIFY out for roster friends only', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_FRIEND_LIST_REQ) {
        return { code: 0, friends: [{ userId: 'user_b', status: 2 }], totalCount: 1 };
      }
      return { code: 0 };
    });
    await h.api.login('user_a');
    const seen: Array<[string, PresenceStatus, string]> = [];
    const off = h.api.onFriendOnline((userId, status, statusMessage) =>
      seen.push([userId, status, statusMessage]));

    h.conn.emit(MsgID.FRIEND_ONLINE_NOTIFY, frame({ userId: 'user_b', status: PresenceStatus.ONLINE }));
    // 自己不弹；名册外用户不弹（初次拉名单前/已删除好友不会误报）。
    h.conn.emit(MsgID.FRIEND_ONLINE_NOTIFY, frame({ userId: 'user_a', status: PresenceStatus.ONLINE }));
    h.conn.emit(MsgID.FRIEND_ONLINE_NOTIFY, frame({ userId: 'user_ghost', status: PresenceStatus.ONLINE }));
    // 畸形体静默丢弃。
    h.conn.emit(MsgID.FRIEND_ONLINE_NOTIFY, new Uint8Array([0xff]));
    expect(seen).toEqual([['user_b', PresenceStatus.ONLINE, '']]);

    off();
    h.conn.emit(MsgID.FRIEND_ONLINE_NOTIFY, frame({ userId: 'user_b', status: PresenceStatus.ONLINE }));
    expect(seen.length).toBe(1);
  });

  it('roundtrips the notify prefs (3025/3027)', async () => {
    const h = makeHarness();
    let setReq: unknown;
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.SET_NOTIFY_PREFS_REQ) setReq = req;
      if (msgId === MsgID.GET_NOTIFY_PREFS_REQ) {
        return { code: 0, prefs: { friendOnlineNotify: false } };
      }
      return { code: 0 };
    });
    expect(await h.api.setNotifyPrefs(false)).toBe(0);
    expect(setReq).toMatchObject({ userId: 'user_a', prefs: { friendOnlineNotify: false } });
    expect(await h.api.getNotifyPrefs()).toBe(false);
  });

  it('degrades prefs calls without a login', async () => {
    const h = makeHarness();
    h.auth.set(() => ({ userId: '', kicked: false, deviceId: '', loggedIn: false }));
    expect(await h.api.setNotifyPrefs(false)).toBe(-1);
    expect(await h.api.getNotifyPrefs()).toBeNull();
  });
});
