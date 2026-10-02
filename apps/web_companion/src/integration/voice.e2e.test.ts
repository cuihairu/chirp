import { MsgID } from '@chirp/proto/gateway';
import {
  ParticipantJoinedNotify,
  ParticipantLeftNotify,
  ParticipantStateChangedNotify,
  RoomType,
} from '@chirp/proto/voice';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ChirpClient } from '@chirp/app-protocol/chirp_client';
import {
  CREATE_ROOM,
  GET_ROOM_INFO,
  GET_USER_ROOM,
  JOIN_ROOM,
  LEAVE_ROOM,
  LOGIN,
  SET_MUTE,
} from '@chirp/app-protocol/msg_map';
import { nextNotify } from './helpers';

/**
 * Real-backend round-trips against chirp_voice (protocol plane: room
 * lifecycle + roster notifies; no audio). Skipped entirely unless
 * CHIRP_VOICE_WS_URL is set — scripts/web_smoke.sh starts the service and
 * points this suite at it alongside the chat/social suites.
 */
const suite = process.env.CHIRP_VOICE_WS_URL ? describe : describe.skip;

// Same per-run suffix discipline as the chat/social suites: the service keeps
// process-lifetime room state and a stale run must not leak in.
const RUN = Date.now().toString(36);
const A = `web_voice_a_${RUN}`;
const B = `web_voice_b_${RUN}`;

/** Voice login: same LOGIN pair, scaffold token = user id. */
async function loginVoiceClient(userId: string): Promise<ChirpClient> {
  const client = new ChirpClient({ url: process.env.CHIRP_VOICE_WS_URL as string });
  await client.connect();
  const resp = await client.request(LOGIN, { token: userId, platform: 'web' });
  if (resp.code !== 0) {
    client.disconnect();
    throw new Error(`voice login failed for ${userId}: code=${resp.code}`);
  }
  client.resetBackoff();
  return client;
}

suite('voice integration (CHIRP_VOICE_WS_URL)', () => {
  let userA: ChirpClient;
  let userB: ChirpClient;
  let roomId: string;

  beforeAll(async () => {
    userA = await loginVoiceClient(A);
    userB = await loginVoiceClient(B);
  });

  afterAll(() => {
    userA?.disconnect();
    userB?.disconnect();
  });

  it('creates a room and joins with the empty scaffold offer', async () => {
    const created = await userA.request(CREATE_ROOM, {
      userId: A,
      roomType: RoomType.GROUP,
      roomName: 'e2e-room',
      maxParticipants: 5,
    });
    expect(created.code).toBe(0);
    expect(created.roomId).not.toBe('');
    roomId = created.roomId;

    const joined = await userA.request(JOIN_ROOM, { userId: A, roomId, sdpOffer: '' });
    expect(joined.code).toBe(0);
    expect(joined.roomId).toBe(roomId);
    // The scaffold answer echoes the (empty) offer — media is out of scope.
    expect(joined.sdpAnswer).toBe('');

    const info = await userA.request(GET_ROOM_INFO, { roomId });
    expect(info.code).toBe(0);
    expect(info.participants.map((p) => p.userId)).toEqual([A]);
  });

  it('pushes PARTICIPANT_JOINED to the other participants', async () => {
    const joinedPromise = nextNotify(
      userA,
      MsgID.PARTICIPANT_JOINED_NOTIFY,
      (raw) => ParticipantJoinedNotify.decode(raw),
      (n) => n.roomId === roomId && n.participant?.userId === B,
    );
    const joined = await userB.request(JOIN_ROOM, { userId: B, roomId, sdpOffer: '' });
    expect(joined.code).toBe(0);
    // The joiner is excluded from the broadcast, the roster pull covers it.
    const notify = await joinedPromise;
    expect(notify.participant?.state).toBe(1 /* CONNECTED */);
  });

  it('broadcasts mute state changes to the room, actor excluded', async () => {
    const changedPromise = nextNotify(
      userA,
      MsgID.PARTICIPANT_STATE_CHANGED_NOTIFY,
      (raw) => ParticipantStateChangedNotify.decode(raw),
      (n) => n.roomId === roomId && n.userId === B,
    );
    const muted = await userB.request(SET_MUTE, { userId: B, roomId, muted: true });
    expect(muted.code).toBe(0);
    expect((await changedPromise).state).toBe(2 /* MUTED */);
  });

  it('pushes PARTICIPANT_LEFT on leave and restores membership afterwards', async () => {
    const leftPromise = nextNotify(
      userA,
      MsgID.PARTICIPANT_LEFT_NOTIFY,
      (raw) => ParticipantLeftNotify.decode(raw),
      (n) => n.roomId === roomId && n.userId === B,
    );
    const left = await userB.request(LEAVE_ROOM, { userId: B, roomId });
    expect(left.code).toBe(0);
    expect((await leftPromise).userId).toBe(B);

    // Refresh-survival: A is still a member per GET_USER_ROOM.
    const mine = await userA.request(GET_USER_ROOM, { userId: A });
    expect(mine.code).toBe(0);
    expect(mine.roomId).toBe(roomId);

    const gone = await userA.request(LEAVE_ROOM, { userId: A, roomId });
    expect(gone.code).toBe(0);
  });
});
