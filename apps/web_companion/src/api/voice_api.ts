import { MsgID } from '@chirp/proto/gateway';
import {
  ParticipantInfo,
  ParticipantJoinedNotify,
  ParticipantLeftNotify,
  ParticipantStateChangedNotify,
  RoomType,
} from '@chirp/proto/voice';
import {
  CREATE_ROOM,
  GET_ROOM_INFO,
  GET_USER_ROOM,
  JOIN_ROOM,
  LEAVE_ROOM,
  LOGIN,
  SET_DEAFEN,
  SET_MUTE,
} from '@chirp/app-protocol/msg_map';
import {
  applyRoomSnapshot,
  applySelfFlags,
  clearRoom,
  removeParticipant,
  setMediaError,
  updateParticipantState,
  upsertParticipant,
  type VoiceState,
} from '../state/voice_store';
import { MediaPermissionError, VoiceMediaApi } from './voice_media';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import type { ChatConnection } from './chat_api';

export interface VoiceApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  voice: Store<VoiceState>;
  /** Media plane override for tests; a real one is created when absent. */
  media?: VoiceMediaApi;
}

/**
 * Voice plane api (fifth websocket, port 9001): room lifecycle and roster
 * sync over the protocol layer. Degradeable like the party plane — when the
 * socket is unavailable chat keeps working and voice features hide.
 *
 * Media plane: VoiceMediaApi drives the WebRTC mesh (capture, per-peer
 * RTCPeerConnection, SDP/ICE relay 4007-4009, AudioContext playback and
 * speaking detection). This class owns the lifecycle: join/create/restore
 * bring it up, leave/eviction tears it down, the mute/deafen echoes gate it.
 * SPEAKING_NOTIFY (4021) still has no server producer — speaking indicators
 * are client-measured from each stream's analyser.
 */
export class VoiceApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly voice: Store<VoiceState>;
  private readonly media: VoiceMediaApi;
  private unsubs: Array<() => void> = [];

  constructor(deps: VoiceApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.voice = deps.voice;
    this.media = deps.media ?? new VoiceMediaApi({ conn: deps.conn, voice: deps.voice });
  }

  async login(userId: string): Promise<boolean> {
    if (this.conn.status !== 'connected') {
      await this.conn.connect();
    }
    const resp = await this.conn.request(LOGIN, { token: userId, platform: 'web' });
    if (resp.code !== 0) {
      this.conn.disconnect();
      return false;
    }
    this.conn.resetBackoff();
    // Notify handlers up before the restore so nothing racing it is lost,
    // then re-adopt an in-progress room membership (page refresh, reconnect).
    this.media.stop();
    this.voice.set((prev) => ({ ...prev, room: null, mediaError: null }));
    this.start();
    await this.restoreRoom();
    return true;
  }

  logout(): void {
    this.stop();
    this.conn.disconnect();
  }

  stop(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  /** Re-adopt our current room (reconnect, login race recovery). */
  async restoreRoom(): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(GET_USER_ROOM, { userId });
    if (resp.code !== 0 || resp.roomId === '') {
      clearRoom(this.voice);
      return;
    }
    await this.refreshRoom(resp.roomId);
    // Restored rooms re-enter media blind: the join response that carries
    // the TURN credentials is long gone, so host candidates only.
    await this.startMedia(resp.roomId, []);
  }

  /**
   * Brings the media plane up for the current roster. A permission failure
   * keeps the roster surface alive and surfaces the browser's error name
   * for the UI instead of failing the room join silently.
   */
  private async startMedia(
    roomId: string,
    iceServers: readonly RTCIceServer[],
  ): Promise<void> {
    const userId = this.auth.get().userId;
    const room = this.voice.get().room;
    if (!userId || !room || room.roomId !== roomId) return;
    const selfDeafened =
      room.participants.find((p) => p.userId === userId)?.deafened ?? false;
    try {
      await this.media.start(
        userId,
        roomId,
        room.participants.map((p) => p.userId),
        iceServers,
        selfDeafened,
      );
      setMediaError(this.voice, null);
    } catch (e) {
      setMediaError(this.voice, e instanceof MediaPermissionError ? e.message : String(e));
    }
  }

  async createRoom(roomName = '', maxParticipants = 0): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const created = await this.conn.request(CREATE_ROOM, {
      userId,
      roomType: RoomType.GROUP,
      roomName,
      maxParticipants,
    });
    if (created.code !== 0) return created.code;
    // Creating does not join: the room sits at zero participants until its
    // creator sends JOIN_ROOM like any other member (the service-side rule,
    // caught by the browser media E2E — the roster pull used to come back
    // empty and nobody ever dialed).
    const resp = await this.conn.request(JOIN_ROOM, {
      userId,
      roomId: created.roomId,
      sdpOffer: '',
    });
    if (resp.code !== 0) return resp.code;
    await this.refreshRoom(created.roomId);
    await this.startMedia(created.roomId, resp.iceServers ?? []);
    return created.code;
  }

  /**
   * The empty sdp_offer stays: the mesh negotiates over the SDP/ICE relay
   * frames (4007-4009), not through the join response.
   */
  async joinRoom(roomId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId || !roomId) return -1;
    const resp = await this.conn.request(JOIN_ROOM, { userId, roomId, sdpOffer: '' });
    if (resp.code !== 0) return resp.code;
    // JOIN_ROOM_RESP carries ids (and the TURN credentials) only; the
    // roster pull fills the details.
    await this.refreshRoom(roomId);
    await this.startMedia(roomId, resp.iceServers ?? []);
    return resp.code;
  }

  async leaveRoom(): Promise<boolean> {
    const userId = this.auth.get().userId;
    const roomId = this.voice.get().room?.roomId;
    if (!userId || !roomId) return false;
    const resp = await this.conn.request(LEAVE_ROOM, { userId, roomId });
    if (resp.code !== 0) return false;
    this.media.stop();
    clearRoom(this.voice);
    return true;
  }

  async setMute(muted: boolean): Promise<number> {
    return this.setFlag(SET_MUTE, 'muted', muted);
  }

  async setDeafen(deafened: boolean): Promise<number> {
    return this.setFlag(SET_DEAFEN, 'deafened', deafened);
  }

  /** Full roster pull; the one source of muted/deafened details. */
  async refreshRoom(roomId: string): Promise<number> {
    const resp = await this.conn.request(GET_ROOM_INFO, { roomId });
    if (resp.code !== 0) return resp.code;
    applyRoomSnapshot(this.voice, {
      roomId: resp.roomId,
      roomName: resp.roomName,
      maxParticipants: resp.maxParticipants,
      participants: (resp.participants ?? []).map(toParticipant),
    });
    return resp.code;
  }

  private async setFlag(
    spec: typeof SET_MUTE | typeof SET_DEAFEN,
    field: 'muted' | 'deafened',
    value: boolean,
  ): Promise<number> {
    const userId = this.auth.get().userId;
    const roomId = this.voice.get().room?.roomId;
    if (!userId || !roomId) return -1;
    const resp = await this.conn.request(spec, { userId, roomId, [field]: value });
    if (resp.code !== 0) return resp.code;
    // The state-changed broadcast excludes the actor: echo locally instead.
    applySelfFlags(this.voice, userId, { [field]: value });
    if (field === 'muted') this.media.setMuted(value);
    else this.media.setDeafened(value);
    return resp.code;
  }

  private start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.PARTICIPANT_JOINED_NOTIFY, (body) =>
        this.onJoinedNotify(body),
      ),
      this.conn.onNotify(MsgID.PARTICIPANT_LEFT_NOTIFY, (body) => this.onLeftNotify(body)),
      this.conn.onNotify(MsgID.PARTICIPANT_STATE_CHANGED_NOTIFY, (body) =>
        this.onStateChangedNotify(body),
      ),
      // SPEAKING_NOTIFY / ICE_CANDIDATE / SDP_OFFER / SDP_ANSWER are the media
      // plane (see class doc): arriving frames are simply not subscribed.
    ];
  }

  private onJoinedNotify(body: Uint8Array): void {
    let notify: ParticipantJoinedNotify;
    try {
      notify = ParticipantJoinedNotify.decode(body);
    } catch {
      return;
    }
    if (notify.roomId !== this.voice.get().room?.roomId || !notify.participant) return;
    upsertParticipant(this.voice, toParticipant(notify.participant));
  }

  private onLeftNotify(body: Uint8Array): void {
    let notify: ParticipantLeftNotify;
    try {
      notify = ParticipantLeftNotify.decode(body);
    } catch {
      return;
    }
    removeParticipant(this.voice, notify.roomId, notify.userId);
    if (notify.userId === this.auth.get().userId) {
      // Evicted (host dissolved the room, kick...): full teardown.
      this.media.stop();
      clearRoom(this.voice);
    } else {
      this.media.dropPeer(notify.userId);
    }
  }

  private onStateChangedNotify(body: Uint8Array): void {
    let notify: ParticipantStateChangedNotify;
    try {
      notify = ParticipantStateChangedNotify.decode(body);
    } catch {
      return;
    }
    updateParticipantState(this.voice, notify.roomId, notify.userId, notify.state);
  }
}

function toParticipant(info: ParticipantInfo) {
  return {
    userId: info.userId,
    state: info.state,
    muted: info.muted,
    deafened: info.deafened,
  };
}
