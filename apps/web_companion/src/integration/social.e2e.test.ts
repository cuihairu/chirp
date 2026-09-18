import { PresenceStatus } from '@chirp/proto/social';
import {
  FriendAcceptedNotify,
  FriendRequestNotify,
  PresenceNotify,
} from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ChirpClient } from '../protocol/chirp_client';
import {
  ADD_FRIEND,
  FRIEND_REQUEST_ACTION,
  GET_PRESENCE,
  LOGIN,
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

    const read = await userA.request(GET_PRESENCE, { userIds: [A, B] });
    expect(read.code).toBe(0);
    const byUser = new Map((read.presences ?? []).map((p) => [p.userId, p]));
    expect(byUser.get(A)?.status).toBe(PresenceStatus.ONLINE);
    expect(byUser.get(A)?.statusMessage).toBe('e2e-here');
    // A user with no snapshot still answers — as an offline placeholder.
    expect(byUser.get(B)?.status).toBe(PresenceStatus.OFFLINE);
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

    // B accepts; the ACCEPTED notify goes to the requester carrying the
    // requester's own id (server quirk — see social_api.ts).
    const actionResp = await userB.request(FRIEND_REQUEST_ACTION, {
      userId: B,
      requestId: request.requestId,
      accept: true,
    });
    expect(actionResp.code).toBe(0);
    const accepted = await nextNotify(
      userA,
      MsgID.FRIEND_ACCEPTED_NOTIFY,
      (body) => FriendAcceptedNotify.decode(body),
      () => true,
    );
    expect(accepted.userId).toBe(A);

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
});
