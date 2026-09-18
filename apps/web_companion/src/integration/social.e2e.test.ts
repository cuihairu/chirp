import { PresenceStatus } from '@chirp/proto/social';
import {
  FriendAcceptedNotify,
  FriendRemovedNotify,
  FriendRequestNotify,
  PresenceNotify,
} from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ChirpClient } from '../protocol/chirp_client';
import {
  ADD_FRIEND,
  FRIEND_REQUEST_ACTION,
  GET_FRIEND_LIST,
  GET_PENDING_REQUESTS,
  GET_PRESENCE,
  LOGIN,
  REMOVE_FRIEND,
  SET_PRESENCE,
} from '../protocol/msg_map';
import { nextNotify } from './helpers';

/**
 * Real-backend round-trips against chirp_social. Skipped entirely unless
 * CHIRP_SOCIAL_WS_URL is set — scripts/web_smoke.sh starts the service and
 * points this suite at it alongside the chat suite.
 */
const suite = process.env.CHIRP_SOCIAL_WS_URL ? describe : describe.skip;

// Same per-run suffix discipline as the chat suite: the service keeps
// process-lifetime friendship state and a stale run must not leak in.
const RUN = Date.now().toString(36);
const A = `web_soc_a_${RUN}`;
const B = `web_soc_b_${RUN}`;

/** Social login: same LOGIN pair, scaffold token = user id, no device id. */
async function loginSocialClient(userId: string): Promise<ChirpClient> {
  const client = new ChirpClient({ url: process.env.CHIRP_SOCIAL_WS_URL as string });
  await client.connect();
  const resp = await client.request(LOGIN, { token: userId, platform: 'web' });
  if (resp.code !== 0) {
    client.disconnect();
    throw new Error(`social login failed for ${userId}: code=${resp.code}`);
  }
  client.resetBackoff();
  return client;
}

suite('social integration (CHIRP_SOCIAL_WS_URL)', () => {
  let userA: ChirpClient;
  let userB: ChirpClient;
  // Never logs in — proves the offline placeholder for unknown users.
  const C = `web_soc_c_${RUN}`;

  beforeAll(async () => {
    userA = await loginSocialClient(A);
    userB = await loginSocialClient(B);
  });

  afterAll(() => {
    userA?.disconnect();
    userB?.disconnect();
  });

  it('stores own presence and answers bulk reads with an offline placeholder', async () => {
    const resp = await userA.request(SET_PRESENCE, {
      userId: A,
      status: PresenceStatus.ONLINE,
      statusMessage: 'e2e-here',
    });
    expect(resp.code).toBe(0);

    const read = await userA.request(GET_PRESENCE, { userIds: [A, B, C] });
    expect(read.code).toBe(0);
    const byUser = new Map((read.presences ?? []).map((p) => [p.userId, p]));
    expect(byUser.get(A)?.status).toBe(PresenceStatus.ONLINE);
    expect(byUser.get(A)?.statusMessage).toBe('e2e-here');
    // B never called SET_PRESENCE but IS logged in — login itself flips
    // presence to ONLINE (broadcast semantics landed with the backend rework).
    expect(byUser.get(B)?.status).toBe(PresenceStatus.ONLINE);
    // A user with no snapshot still answers — as an offline placeholder.
    expect(byUser.get(C)?.status).toBe(PresenceStatus.OFFLINE);
  });

  it('rejects a self-add', async () => {
    const resp = await userA.request(ADD_FRIEND, { userId: A, targetUserId: A });
    expect(resp.code).not.toBe(0);
  });

  it('runs the friend handshake and gates presence broadcast on friendship', async () => {
    // A requests; B is online and gets the request pushed.
    const requestPushed = nextNotify(
      userB,
      MsgID.FRIEND_REQUEST_NOTIFY,
      (body) => FriendRequestNotify.decode(body),
      (n) => n.fromUserId === A,
    );
    const addResp = await userA.request(ADD_FRIEND, { userId: A, targetUserId: B });
    expect(addResp.code).toBe(0);
    const request = await requestPushed;
    expect(request.requestId).toBeTruthy();

    // B accepts; the ACCEPTED notify goes to BOTH sides, each carrying the
    // OTHER party's user_id. Both listeners MUST register before the action:
    // the server sends the notifies before the action response, so a listener
    // created after `await action` misses the frame already dispatched.
    const acceptedAtB = nextNotify(
      userB,
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      (body) => FriendAcceptedNotify.decode(body),
      () => true,
    );
    const acceptedAtA = nextNotify(
      userA,
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      (body) => FriendAcceptedNotify.decode(body),
      () => true,
    );
    const actionResp = await userB.request(FRIEND_REQUEST_ACTION, {
      userId: B,
      requestId: request.requestId,
      accept: true,
    });
    expect(actionResp.code).toBe(0);
    expect((await acceptedAtA).userId).toBe(B);
    expect((await acceptedAtB).userId).toBe(A);

    // The server-authoritative roster now shows the friendship on both
    // sides, and the settled request left no pending queue entry.
    const listA = await userA.request(GET_FRIEND_LIST, { userId: A, limit: 0, offset: 0 });
    expect(listA.code).toBe(0);
    expect((listA.friends ?? []).map((f) => f.userId)).toContain(B);
    const listB = await userB.request(GET_FRIEND_LIST, { userId: B, limit: 0, offset: 0 });
    expect((listB.friends ?? []).map((f) => f.userId)).toContain(A);
    const pendingA = await userA.request(GET_PENDING_REQUESTS, { userId: A });
    expect(pendingA.requests ?? []).toHaveLength(0);

    // Now that they are friends, B's presence changes broadcast to A.
    const presencePushed = nextNotify(
      userA,
      MsgID.PRESENCE_NOTIFY,
      (body) => PresenceNotify.decode(body),
      (n) => n.userId === B,
    );
    const awayResp = await userB.request(SET_PRESENCE, {
      userId: B,
      status: PresenceStatus.AWAY,
    });
    expect(awayResp.code).toBe(0);
    const pushed = await presencePushed;
    expect(pushed.status).toBe(PresenceStatus.AWAY);

    // Re-adding an existing friend is a server-side error.
    const again = await userA.request(ADD_FRIEND, { userId: A, targetUserId: B });
    expect(again.code).not.toBe(0);
  });

  it('removes the friendship symmetrically and notifies the peer', async () => {
    const removedAtB = nextNotify(
      userB,
      MsgID.FRIEND_REMOVED_NOTIFY,
      (body) => FriendRemovedNotify.decode(body),
      (n) => n.userId === A,
    );
    const rm = await userA.request(REMOVE_FRIEND, { userId: A, friendUserId: B });
    expect(rm.code).toBe(0);
    const removed = await removedAtB;
    expect(removed.userId).toBe(A);

    // Both rosters are empty afterwards; repeating the remove stays OK.
    const listA = await userA.request(GET_FRIEND_LIST, { userId: A, limit: 0, offset: 0 });
    expect((listA.friends ?? []).filter((f) => f.userId === B)).toHaveLength(0);
    const listB = await userB.request(GET_FRIEND_LIST, { userId: B, limit: 0, offset: 0 });
    expect((listB.friends ?? []).filter((f) => f.userId === A)).toHaveLength(0);
    const repeat = await userA.request(REMOVE_FRIEND, { userId: A, friendUserId: B });
    expect(repeat.code).toBe(0);
  });

  it('declining a fresh request clears pending on both sides without a friendship', async () => {
    const requestPushed = nextNotify(
      userB,
      MsgID.FRIEND_REQUEST_NOTIFY,
      (body) => FriendRequestNotify.decode(body),
      (n) => n.fromUserId === A,
    );
    const addResp = await userA.request(ADD_FRIEND, { userId: A, targetUserId: B });
    expect(addResp.code).toBe(0);
    const request = await requestPushed;

    const pendingB = await userB.request(GET_PENDING_REQUESTS, { userId: B });
    expect((pendingB.requests ?? []).map((r) => r.requestId)).toContain(request.requestId);

    const actionResp = await userB.request(FRIEND_REQUEST_ACTION, {
      userId: B,
      requestId: request.requestId,
      accept: false,
    });
    expect(actionResp.code).toBe(0);

    const pendingBAfter = await userB.request(GET_PENDING_REQUESTS, { userId: B });
    expect(pendingBAfter.requests ?? []).toHaveLength(0);
    const listB = await userB.request(GET_FRIEND_LIST, { userId: B, limit: 0, offset: 0 });
    expect((listB.friends ?? []).filter((f) => f.userId === A)).toHaveLength(0);
  });
});
