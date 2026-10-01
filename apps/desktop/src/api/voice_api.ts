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
} from '@chirp/protocol/msg_map';
import {
  applyRoomSnapshot,
  applySelfFlags,
  clearRoom,
  removeParticipant,
  updateParticipantState,
  upsertParticipant,
  type VoiceState,
} from '../state/voice_store';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import type { ChatConnection } from './chat_api';

export interface VoiceApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  voice: Store<VoiceState>;
}

/**
 * Voice plane api (fifth websocket, port 9001): room lifecycle and roster
 * sync over the protocol layer. Degradeable like the party plane — when the
 * socket is unavailable chat keeps working and voice features hide.
 *
 * Boundary: this is the signalling/roster surface only. The SDP/ICE relay
 * (4007-4009) and SPEAKING_NOTIFY (4021) belong to a WebRTC media plane that
 * the web companion does not ship; join sends an empty sdp_offer and the
 * room UI renders roster state, not audio.
 */
export class VoiceApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly voice: Store<VoiceState>;
  private unsubs: Array<() => void> = [];

  constructor(deps: VoiceApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.voice = deps.voice;
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
    this.voice.set((prev) => ({ ...prev, room: null }));
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
  }

  async createRoom(roomName = '', maxParticipants = 0): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(CREATE_ROOM, {
      userId,
      roomType: RoomType.GROUP,
      roomName,
      maxParticipants,
    });
    if (resp.code !== 0) return resp.code;
    await this.refreshRoom(resp.roomId);
    return resp.code;
  }

  /** The empty sdp_offer is echoed back; media negotiation is out of scope. */
  async joinRoom(roomId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId || !roomId) return -1;
    const resp = await this.conn.request(JOIN_ROOM, { userId, roomId, sdpOffer: '' });
    if (resp.code !== 0) return resp.code;
    // JOIN_ROOM_RESP carries ids only; the roster pull fills the details.
    await this.refreshRoom(roomId);
    return resp.code;
  }

  async leaveRoom(): Promise<boolean> {
    const userId = this.auth.get().userId;
    const roomId = this.voice.get().room?.roomId;
    if (!userId || !roomId) return false;
    const resp = await this.conn.request(LEAVE_ROOM, { userId, roomId });
    if (resp.code !== 0) return false;
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
