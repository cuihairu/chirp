import { describe, expect, it, vi } from 'vitest';
import { ParticipantState } from '@chirp/proto/voice';
import {
  applyRoomSnapshot,
  applySelfFlags,
  clearRoom,
  createVoiceStore,
  derivedState,
  removeParticipant,
  selfParticipantOf,
  updateParticipantState,
  upsertParticipant,
  type VoiceRoomSnapshot,
} from './voice_store';

const room = (over: Partial<VoiceRoomSnapshot> = {}): VoiceRoomSnapshot => ({
  roomId: 'room-1',
  roomName: 'smoke-room',
  maxParticipants: 5,
  participants: [{ userId: 'user_a', state: ParticipantState.CONNECTED }],
  ...over,
});

describe('voice_store', () => {
  it('applies a roster snapshot and clears on leave', () => {
    const store = createVoiceStore();
    expect(store.get().room).toBeNull();
    applyRoomSnapshot(store, room());
    expect(store.get().room?.roomId).toBe('room-1');
    expect(store.get().room?.participants).toHaveLength(1);
    clearRoom(store);
    expect(store.get().room).toBeNull();
  });

  it('clearing an already-empty room skips the publish', () => {
    const store = createVoiceStore();
    const listener = vi.fn();
    store.subscribe(listener);
    clearRoom(store);
    expect(listener).not.toHaveBeenCalled();
  });

  it('upsertParticipant replaces an existing row for the same user', () => {
    const store = createVoiceStore();
    applyRoomSnapshot(store, room());
    upsertParticipant(store, {
      userId: 'user_b',
      state: ParticipantState.CONNECTED,
    });
    upsertParticipant(store, { userId: 'user_b', state: ParticipantState.MUTED });
    expect(store.get().room?.participants.map((p) => p.userId)).toEqual([
      'user_a',
      'user_b',
    ]);
    expect(selfParticipantOf(store.get(), 'user_b')?.state).toBe(ParticipantState.MUTED);
  });

  it('upsertParticipant is a no-op without a held room', () => {
    const store = createVoiceStore();
    upsertParticipant(store, { userId: 'user_b', state: ParticipantState.CONNECTED });
    expect(store.get().room).toBeNull();
  });

  it('removeParticipant ignores a stale roomId but drops matching rows', () => {
    const store = createVoiceStore();
    applyRoomSnapshot(
      store,
      room({ participants: [room().participants[0], { userId: 'user_b', state: 1 }] }),
    );
    removeParticipant(store, 'room-stale', 'user_b');
    expect(store.get().room?.participants).toHaveLength(2);
    removeParticipant(store, 'room-1', 'user_b');
    expect(store.get().room?.participants.map((p) => p.userId)).toEqual(['user_a']);
  });

  it('updateParticipantState patches only the touched row and guards roomId', () => {
    const store = createVoiceStore();
    applyRoomSnapshot(
      store,
      room({ participants: [room().participants[0], { userId: 'user_b', state: 1 }] }),
    );
    updateParticipantState(store, 'room-stale', 'user_a', ParticipantState.MUTED);
    expect(store.get().room?.participants[0].state).toBe(ParticipantState.CONNECTED);
    updateParticipantState(store, 'room-1', 'user_a', ParticipantState.MUTED);
    expect(store.get().room?.participants[0].state).toBe(ParticipantState.MUTED);
    expect(store.get().room?.participants[1].state).toBe(ParticipantState.CONNECTED);
  });

  it('applySelfFlags re-derives own state with DEAFENED > MUTED > CONNECTED', () => {
    const store = createVoiceStore();
    applyRoomSnapshot(store, room());
    applySelfFlags(store, 'user_a', { muted: true });
    expect(selfParticipantOf(store.get(), 'user_a')).toEqual({
      userId: 'user_a',
      state: ParticipantState.MUTED,
      muted: true,
      deafened: false,
    });
    applySelfFlags(store, 'user_a', { deafened: true });
    expect(selfParticipantOf(store.get(), 'user_a')?.state).toBe(ParticipantState.DEAFENED);
    applySelfFlags(store, 'user_a', { muted: false, deafened: false });
    expect(selfParticipantOf(store.get(), 'user_a')?.state).toBe(ParticipantState.CONNECTED);
  });

  it('applySelfFlags leaves strangers untouched and no-ops without a room', () => {
    const store = createVoiceStore();
    applySelfFlags(store, 'user_a', { muted: true });
    expect(store.get().room).toBeNull();
    applyRoomSnapshot(
      store,
      room({ participants: [room().participants[0], { userId: 'user_b', state: 1 }] }),
    );
    applySelfFlags(store, 'user_a', { muted: true });
    expect(selfParticipantOf(store.get(), 'user_b')?.muted).toBeUndefined();
    expect(selfParticipantOf(store.get(), 'user_b')?.state).toBe(ParticipantState.CONNECTED);
  });

  it('derivedState mirrors the service precedence table', () => {
    expect(derivedState(false, false)).toBe(ParticipantState.CONNECTED);
    expect(derivedState(true, false)).toBe(ParticipantState.MUTED);
    expect(derivedState(true, true)).toBe(ParticipantState.DEAFENED);
    expect(derivedState(false, true)).toBe(ParticipantState.DEAFENED);
  });
});
