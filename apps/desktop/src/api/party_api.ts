import { MsgID } from '@chirp/proto/gateway';
import {
  PartyStateChangedNotify,
  PartyDisbandedNotify,
  PartyKickedNotify,
  InviteNotify,
} from '@chirp/proto/party';
import {
  ACCEPT_PARTY_INVITE,
  CREATE_PARTY,
  DECLINE_PARTY_INVITE,
  DISBAND_PARTY,
  GET_MY_PARTY,
  INVITE_TO_PARTY,
  KICK_PARTY_MEMBER,
  LEAVE_PARTY,
  LOGIN,
  SET_PARTY_READY,
  TRANSFER_PARTY_LEADER,
} from '@chirp/protocol/msg_map';
import {
  addInvite,
  applySnapshot,
  clearParty,
  removeInvite,
  resetInvites,
  type PartySnapshot,
  type PartyState,
} from '../state/party_store';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import type { ChatConnection } from './chat_api';

export interface PartyApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  party: Store<PartyState>;
}

/**
 * Party plane api (third websocket, port 7501): cross-game team-up.
 * Degradeable like the social plane — when the socket is unavailable chat
 * keeps working and party features hide. Sync is snapshot-based: the server
 * sends the full PartyInfo to every member on every change, so the store
 * applies snapshots and never diffs events.
 */
export class PartyApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly party: Store<PartyState>;
  private unsubs: Array<() => void> = [];

  constructor(deps: PartyApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.party = deps.party;
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
    // Notify handlers up before the pull so nothing racing it is lost, then
    // restore an in-progress membership (page refresh, reconnect).
    this.party.set((prev) => ({ ...prev, party: null }));
    resetInvites(this.party);
    this.start();
    await this.fetchMyParty();
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

  /** Re-pull our current membership (reconnect, login race recovery). */
  async fetchMyParty(): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(GET_MY_PARTY, { userId });
    if (resp.code !== 0) return resp.code;
    if (resp.inParty && resp.party) {
      applySnapshot(this.party, toSnapshot(resp.party));
    } else {
      clearParty(this.party);
    }
    return resp.code;
  }

  async createParty(maxMembers = 0): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(CREATE_PARTY, { userId, maxMembers });
    if (resp.code === 0 && resp.party) {
      applySnapshot(this.party, toSnapshot(resp.party));
    }
    return resp.code;
  }

  async invite(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return -1;
    const resp = await this.conn.request(INVITE_TO_PARTY, { userId, partyId, targetUserId });
    return resp.code;
  }

  async acceptInvite(inviteId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(ACCEPT_PARTY_INVITE, { userId, inviteId });
    if (resp.code === 0) {
      removeInvite(this.party, inviteId);
      if (resp.party) applySnapshot(this.party, toSnapshot(resp.party));
    }
    return resp.code;
  }

  async declineInvite(inviteId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(DECLINE_PARTY_INVITE, { userId, inviteId });
    if (resp.code === 0) removeInvite(this.party, inviteId);
    return resp.code;
  }

  /** Returns true when our leaving silently disbanded the party. */
  async leaveParty(): Promise<boolean> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return false;
    const resp = await this.conn.request(LEAVE_PARTY, { userId, partyId });
    if (resp.code !== 0) return false;
    clearParty(this.party);
    return resp.partyDisbanded;
  }

  /** Leader-only; the members learn via PARTY_LEFT_NOTIFY reason "kicked". */
  async kickMember(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return -1;
    return (await this.conn.request(KICK_PARTY_MEMBER, { userId, partyId, targetUserId })).code;
  }

  /** Leader-only teardown; members learn via PARTY_DISBANDED_NOTIFY. */
  async disbandParty(): Promise<number> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return -1;
    return (await this.conn.request(DISBAND_PARTY, { userId, partyId })).code;
  }

  /** Leader-only succession hand-off. */
  async transferLeader(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return -1;
    return (await this.conn.request(TRANSFER_PARTY_LEADER, { userId, partyId, targetUserId }))
      .code;
  }

  async setReady(ready: boolean): Promise<number> {
    const userId = this.auth.get().userId;
    const partyId = this.party.get().party?.partyId;
    if (!userId || !partyId) return -1;
    return (await this.conn.request(SET_PARTY_READY, { userId, partyId, ready })).code;
  }

  private start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.INVITE_NOTIFY, (body) => this.onInviteNotify(body)),
      this.conn.onNotify(MsgID.PARTY_STATE_CHANGED_NOTIFY, (body) =>
        this.onStateChangedNotify(body),
      ),
      this.conn.onNotify(MsgID.PARTY_KICKED_NOTIFY, (body) => this.onKickedNotify(body)),
      this.conn.onNotify(MsgID.PARTY_DISBANDED_NOTIFY, (body) => this.onDisbandedNotify(body)),
      // JOINED/LEFT/INVITE_RESULT need no handling: JOINED and LEFT always
      // ride with (or are superseded by) a full STATE_CHANGED snapshot, and
      // the inviter's outcome surfaces through the UI reading the store.
    ];
  }

  private onInviteNotify(body: Uint8Array): void {
    let notify: InviteNotify;
    try {
      notify = InviteNotify.decode(body);
    } catch {
      return;
    }
    addInvite(this.party, notify.inviteId, notify.fromUserId, notify.party?.partyId ?? '');
  }

  private onStateChangedNotify(body: Uint8Array): void {
    let notify: PartyStateChangedNotify;
    try {
      notify = PartyStateChangedNotify.decode(body);
    } catch {
      return;
    }
    if (notify.party) applySnapshot(this.party, toSnapshot(notify.party));
  }

  private onKickedNotify(body: Uint8Array): void {
    // The kicked user is no longer a member and gets no snapshot; the decode
    // is only a well-formedness gate.
    try {
      PartyKickedNotify.decode(body);
    } catch {
      return;
    }
    clearParty(this.party);
  }

  private onDisbandedNotify(body: Uint8Array): void {
    try {
      PartyDisbandedNotify.decode(body);
    } catch {
      return;
    }
    clearParty(this.party);
  }
}

function toSnapshot(info: {
  partyId: string;
  leaderId: string;
  maxMembers: number;
  members: Array<{ userId: string; ready: boolean }>;
}): PartySnapshot {
  return {
    partyId: info.partyId,
    leaderId: info.leaderId,
    maxMembers: info.maxMembers,
    members: (info.members ?? []).map((m) => ({ userId: m.userId, ready: m.ready })),
  };
}
