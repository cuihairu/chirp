import { ParticipantState } from '@chirp/proto/voice';
import { createStore, type Store } from './store';

/**
 * Client-side mirror of the voice plane (protocol layer: room roster and
 * participant state; no audio). The backend is authoritative but the roster
 * change notifies exclude the actor (join/leave/mute all pass exclude_user),
 * so this mirror is patched from three directions: full roster pulls
 * (GET_ROOM_INFO after join/create/restore), actor-less notifies from the
 * server, and local echoes of our own mute/deafen requests.
 */
export interface VoiceParticipant {
  userId: string;
  /** Server-derived state (DEAFENED > MUTED > CONNECTED). */
  state: ParticipantState;
  /** Flag details only arrive with a full roster pull; notifies carry state only. */
  muted?: boolean;
  deafened?: boolean;
}

/** Narrowed copy of chirp.voice.GetRoomInfoResponse the UI renders. */
export interface VoiceRoomSnapshot {
  roomId: string;
  roomName: string;
  maxParticipants: number;
  participants: VoiceParticipant[];
}

export interface VoiceState {
  /** The room we are sitting in; null when not in a voice room. */
  room: VoiceRoomSnapshot | null;
}

export const createVoiceStore = (
  initial: VoiceState = { room: null },
): Store<VoiceState> => createStore<VoiceState>(initial);

/** Applies a full server roster (GET_ROOM_INFO / join / restore path). */
export function applyRoomSnapshot(store: Store<VoiceState>, snapshot: VoiceRoomSnapshot): void {
  store.set((prev) => ({ ...prev, room: snapshot }));
}

/** Leaving (or a failed restore) drops the mirror entirely. */
export function clearRoom(store: Store<VoiceState>): void {
  store.set((prev) => (prev.room === null ? prev : { ...prev, room: null }));
}

/** PARTICIPANT_JOINED_NOTIFY (never for ourselves — the actor is excluded). */
export function upsertParticipant(
  store: Store<VoiceState>,
  participant: VoiceParticipant,
): void {
  store.set((prev) => {
    if (!prev.room) return prev;
    const rest = prev.room.participants.filter((p) => p.userId !== participant.userId);
    return { ...prev, room: { ...prev.room, participants: [...rest, participant] } };
  });
}

/** PARTICIPANT_LEFT_NOTIFY; a stale roomId (we already left) is ignored. */
export function removeParticipant(
  store: Store<VoiceState>,
  roomId: string,
  userId: string,
): void {
  store.set((prev) => {
    if (!prev.room || prev.room.roomId !== roomId) return prev;
    return {
      ...prev,
      room: {
        ...prev.room,
        participants: prev.room.participants.filter((p) => p.userId !== userId),
      },
    };
  });
}

/** PARTICIPANT_STATE_CHANGED_NOTIFY; the actor applies the echo locally. */
export function updateParticipantState(
  store: Store<VoiceState>,
  roomId: string,
  userId: string,
  state: ParticipantState,
): void {
  store.set((prev) => {
    if (!prev.room || prev.room.roomId !== roomId) return prev;
    return {
      ...prev,
      room: {
        ...prev.room,
        participants: prev.room.participants.map((p) =>
          p.userId === userId ? { ...p, state } : p,
        ),
      },
    };
  });
}

/**
 * Local echo of our own SET_MUTE / SET_DEAFEN (the RESP carries no roster and
 * the broadcast excludes the actor). Re-derives our state exactly like the
 * service does: DEAFENED beats MUTED beats CONNECTED.
 */
export function applySelfFlags(
  store: Store<VoiceState>,
  userId: string,
  flags: { muted?: boolean; deafened?: boolean },
): void {
  store.set((prev) => {
    if (!prev.room) return prev;
    return {
      ...prev,
      room: {
        ...prev.room,
        participants: prev.room.participants.map((p) => {
          if (p.userId !== userId) return p;
          const muted = flags.muted ?? p.muted ?? false;
          const deafened = flags.deafened ?? p.deafened ?? false;
          return { ...p, muted, deafened, state: derivedState(muted, deafened) };
        }),
      },
    };
  });
}

/** Same precedence the service's DeriveParticipantState implements. */
export function derivedState(muted: boolean, deafened: boolean): ParticipantState {
  if (deafened) return ParticipantState.DEAFENED;
  if (muted) return ParticipantState.MUTED;
  return ParticipantState.CONNECTED;
}

/** Our own participant row, if we hold a roster. */
export const selfParticipantOf = (
  state: VoiceState,
  userId: string,
): VoiceParticipant | undefined => state.room?.participants.find((p) => p.userId === userId);
