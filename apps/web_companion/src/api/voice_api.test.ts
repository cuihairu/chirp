import { describe, expect, it } from 'vitest';
import { MsgID } from '@chirp/proto/gateway';
import {
  ParticipantJoinedNotify,
  ParticipantLeftNotify,
  ParticipantState,
  ParticipantStateChangedNotify,
} from '@chirp/proto/voice';
import { VoiceApi } from './voice_api';
import { createStore } from '../state/store';
import { createVoiceStore } from '../state/voice_store';
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
  const voice = createVoiceStore();
  const api = new VoiceApi({ conn, auth, voice });
  conn.setResponder(async () => ({ code: 0 }));
  return { conn, auth, voice, api };
};

const rosterResp = () => ({
  code: 0,
  roomId: 'room-1',
  roomName: 'smoke-room',
  roomType: 1,
  maxParticipants: 5,
  participants: [
    { userId: 'user_a', state: ParticipantState.CONNECTED, muted: false, deafened: false },
    { userId: 'user_b', state: ParticipantState.CONNECTED, muted: false, deafened: false },
  ],
});

describe('VoiceApi.login', () => {
  it('logs in and restores an in-progress room via GET_ROOM_INFO', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_USER_ROOM_REQ) return { code: 0, roomId: 'room-1' };
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return rosterResp();
      return { code: 0 };
    });
    expect(await h.api.login('user_a')).toBe(true);
    expect(h.voice.get().room?.roomId).toBe('room-1');
    expect(h.voice.get().room?.participants).toHaveLength(2);
  });

  it('restores nothing after a clean not-in-room pull', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => ({ code: 0, roomId: '' }));
    expect(await h.api.login('user_a')).toBe(true);
    expect(h.voice.get().room).toBeNull();
  });

  it('disconnects and degrades on a login rejection', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => ({ code: 10 }));
    expect(await h.api.login('user_a')).toBe(false);
    expect(h.voice.get().room).toBeNull();
  });
});

describe('VoiceApi room lifecycle', () => {
  it('creates a room and pulls the roster', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.CREATE_ROOM_REQ) {
        expect((req as { roomType: number }).roomType).toBe(1);
        return { code: 0, roomId: 'room-1' };
      }
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return rosterResp();
      return { code: 0 };
    });
    expect(await h.api.createRoom('smoke-room', 5)).toBe(0);
    expect(h.voice.get().room?.roomName).toBe('smoke-room');
    expect(h.voice.get().room?.maxParticipants).toBe(5);
  });

  it('join sends the empty scaffold offer and refreshes via GET_ROOM_INFO', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.JOIN_ROOM_REQ) {
        expect((req as { sdpOffer: string }).sdpOffer).toBe('');
        expect((req as { roomId: string }).roomId).toBe('room-1');
        return { code: 0, roomId: 'room-1', participantIds: ['user_a', 'user_b'] };
      }
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return rosterResp();
      return { code: 0 };
    });
    expect(await h.api.joinRoom('room-1')).toBe(0);
    expect(h.voice.get().room?.participants).toHaveLength(2);
  });

  it('leave clears the mirror only on success', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => rosterResp());
    await h.api.refreshRoom('room-1');
    h.conn.setResponder(async () => ({ code: 0 }));
    expect(await h.api.leaveRoom()).toBe(true);
    expect(h.voice.get().room).toBeNull();
  });

  it('keeps the mirror when leave is rejected', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => rosterResp());
    await h.api.refreshRoom('room-1');
    h.conn.setResponder(async () => ({ code: 21 }));
    expect(await h.api.leaveRoom()).toBe(false);
    expect(h.voice.get().room?.roomId).toBe('room-1');
  });

  it('requires a held room before leave, mute or deafen', async () => {
    const h = makeHarness();
    expect(await h.api.leaveRoom()).toBe(false);
    expect(await h.api.setMute(true)).toBe(-1);
    expect(await h.api.setDeafen(true)).toBe(-1);
  });

  it('mute and deafen forward the flag and echo it locally', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => rosterResp());
    await h.api.refreshRoom('room-1');
    let lastReq: unknown = null;
    h.conn.setResponder(async (_msgId, req) => {
      lastReq = req;
      return { code: 0 };
    });
    expect(await h.api.setMute(true)).toBe(0);
    expect((lastReq as { muted: boolean }).muted).toBe(true);
    expect(h.voice.get().room?.participants[0].state).toBe(ParticipantState.MUTED);
    expect(await h.api.setDeafen(true)).toBe(0);
    expect((lastReq as { deafened: boolean }).deafened).toBe(true);
    expect(h.voice.get().room?.participants[0].state).toBe(ParticipantState.DEAFENED);
  });

  it('does not echo flags locally when the request fails', async () => {
    const h = makeHarness();
    h.conn.setResponder(async () => rosterResp());
    await h.api.refreshRoom('room-1');
    h.conn.setResponder(async () => ({ code: 21 }));
    expect(await h.api.setMute(true)).toBe(21);
    expect(h.voice.get().room?.participants[0].state).toBe(ParticipantState.CONNECTED);
  });
});

describe('VoiceApi notifies', () => {
  /** login() both registers the notify handlers and restores the roster. */
  const bootInRoom = async (h: ReturnType<typeof makeHarness>): Promise<void> => {
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_USER_ROOM_REQ) return { code: 0, roomId: 'room-1' };
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return rosterResp();
      return { code: 0 };
    });
    await h.api.login('user_a');
  };

  it('applies PARTICIPANT_JOINED only for the room we hold', async () => {
    const h = makeHarness();
    await bootInRoom(h);

    const notify = ParticipantJoinedNotify.fromPartial({
      roomId: 'room-other',
      participant: { userId: 'user_c', state: ParticipantState.CONNECTED },
    });
    h.conn.emit(
      MsgID.PARTICIPANT_JOINED_NOTIFY,
      ParticipantJoinedNotify.encode(notify).finish(),
    );
    expect(h.voice.get().room?.participants).toHaveLength(2);

    const mine = ParticipantJoinedNotify.fromPartial({
      roomId: 'room-1',
      participant: { userId: 'user_c', state: ParticipantState.CONNECTED },
    });
    h.conn.emit(MsgID.PARTICIPANT_JOINED_NOTIFY, ParticipantJoinedNotify.encode(mine).finish());
    expect(h.voice.get().room?.participants.map((p) => p.userId)).toEqual([
      'user_a',
      'user_b',
      'user_c',
    ]);
  });

  it('drops participants on PARTICIPANT_LEFT and updates state on STATE_CHANGED', async () => {
    const h = makeHarness();
    await bootInRoom(h);

    const left = ParticipantLeftNotify.fromPartial({ roomId: 'room-1', userId: 'user_b' });
    h.conn.emit(MsgID.PARTICIPANT_LEFT_NOTIFY, ParticipantLeftNotify.encode(left).finish());
    expect(h.voice.get().room?.participants.map((p) => p.userId)).toEqual(['user_a']);

    const changed = ParticipantStateChangedNotify.fromPartial({
      roomId: 'room-1',
      userId: 'user_a',
      state: ParticipantState.MUTED,
    });
    h.conn.emit(
      MsgID.PARTICIPANT_STATE_CHANGED_NOTIFY,
      ParticipantStateChangedNotify.encode(changed).finish(),
    );
    expect(h.voice.get().room?.participants[0].state).toBe(ParticipantState.MUTED);
    // State-only notify: no flag details invented.
    expect(h.voice.get().room?.participants[0].muted).toBe(false);
  });

  it('ignores malformed notify bodies instead of throwing', async () => {
    const h = makeHarness();
    await bootInRoom(h);
    expect(() =>
      h.conn.emit(
        MsgID.PARTICIPANT_STATE_CHANGED_NOTIFY,
        new Uint8Array([0x01, 0xff, 0x00]),
      ),
    ).not.toThrow();
    expect(h.voice.get().room?.participants).toHaveLength(2);
  });

  it('stops handling notifies after logout', async () => {
    const h = makeHarness();
    await bootInRoom(h);
    h.api.logout();
    const notify = ParticipantJoinedNotify.fromPartial({
      roomId: 'room-1',
      participant: { userId: 'user_c', state: ParticipantState.CONNECTED },
    });
    h.conn.emit(MsgID.PARTICIPANT_JOINED_NOTIFY, ParticipantJoinedNotify.encode(notify).finish());
    expect(h.voice.get().room?.participants).toHaveLength(2);
  });
});