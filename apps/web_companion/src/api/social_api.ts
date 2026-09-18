import { PresenceStatus } from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';
import { ADD_FRIEND, FRIEND_REQUEST_ACTION, GET_PRESENCE, LOGIN, SET_PRESENCE } from '../protocol/msg_map';
import { setPresence } from '../state/presence_store';
import {
  addFriend,
  addPendingIn,
  addPendingOut,
  fromUserIdOf,
  loadFriendState,
  persistFriendStore,
  removeFriend,
  resolvePending,
  type FriendState,
} from '../state/friend_store';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import type { PresenceState } from '../state/presence_store';
import type { ChatConnection } from './chat_api';
import { FriendAcceptedNotify, FriendRequestNotify, FriendRemovedNotify, PresenceNotify } from '@chirp/proto/social';

export interface SocialApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  presence: Store<PresenceState>;
  friends: Store<FriendState>;
}

/**
 * Social plane api (second websocket, port 8001): friends and presence.
 * Degradable by design — when the social socket is unavailable the chat
 * keeps working and the UI simply hides friend features.
 */
export class SocialApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly presence: Store<PresenceState>;
  private readonly friends: Store<FriendState>;
  private unsubs: Array<() => void> = [];
  private persistOff: (() => void) | null = null;

  constructor(deps: SocialApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.presence = deps.presence;
    this.friends = deps.friends;
  }

  async login(userId: string): Promise<boolean> {
    if (this.conn.status !== 'connected') {
      await this.conn.connect();
    }
    // Same LOGIN pair as chat (scaffold token = user id); the social
    // service keeps no device registry, so no device_id is needed.
    const resp = await this.conn.request(LOGIN, { token: userId, platform: 'web' });
    if (resp.code !== 0) {
      // Degrade quietly: no friend features, chat unaffected.
      this.conn.disconnect();
      return false;
    }
    this.conn.resetBackoff();
    this.friends.set(() => loadFriendState(userId));
    this.persistOff = persistFriendStore(this.friends, userId);
    this.start();
    return true;
  }

  logout(): void {
    this.stop();
    this.conn.disconnect();
  }

  stop(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
    this.persistOff?.();
    this.persistOff = null;
  }

  /** Advertise our presence; the service broadcasts it to our friends. */
  async setPresence(status: PresenceStatus, statusMessage = ''): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    await this.conn.request(SET_PRESENCE, { userId, status, statusMessage });
  }

  /** Bulk pull for the visible roster (conversation peers, friends). */
  async pullPresence(userIds: string[]): Promise<void> {
    if (userIds.length === 0) return;
    const resp = await this.conn.request(GET_PRESENCE, { userIds });
    if (resp.code !== 0) return;
    const at = Date.now();
    for (const presence of resp.presences ?? []) {
      setPresence(this.presence, presence.userId, presence.status, presence.statusMessage, at);
    }
  }

  /** Pending-request model: the other side must accept before we are friends. */
  async addFriend(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(ADD_FRIEND, {
      userId,
      targetUserId,
      message: '',
    });
    if (resp.code === 0) addPendingOut(this.friends, targetUserId);
    return resp.code;
  }

  async respondRequest(requestId: string, accept: boolean): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const from = fromUserIdOf(this.friends.get(), requestId);
    const resp = await this.conn.request(FRIEND_REQUEST_ACTION, { userId, requestId, accept });
    if (resp.code !== 0) return resp.code;
    resolvePending(this.friends, requestId);
    // The ACCEPTED notify excludes the actor, so accept-side bookkeeping is local.
    if (accept && from) addFriend(this.friends, from);
    return resp.code;
  }

  private start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.PRESENCE_NOTIFY, (body) => this.onPresenceNotify(body)),
      this.conn.onNotify(MsgID.FRIEND_REQUEST_NOTIFY, (body) => this.onFriendRequestNotify(body)),
      this.conn.onNotify(MsgID.FRIEND_ACCEPTED_NOTIFY, (body) => this.onFriendAcceptedNotify(body)),
      this.conn.onNotify(MsgID.FRIEND_REMOVED_NOTIFY, (body) => this.onFriendRemovedNotify(body)),
    ];
  }

  private onPresenceNotify(body: Uint8Array): void {
    let notify: PresenceNotify;
    try {
      notify = PresenceNotify.decode(body);
    } catch {
      return;
    }
    setPresence(this.presence, notify.userId, notify.status, notify.statusMessage, Date.now());
  }

  private onFriendRequestNotify(body: Uint8Array): void {
    let notify: FriendRequestNotify;
    try {
      notify = FriendRequestNotify.decode(body);
    } catch {
      return;
    }
    addPendingIn(this.friends, notify.requestId, notify.fromUserId);
  }

  private onFriendAcceptedNotify(body: Uint8Array): void {
    let notify: FriendAcceptedNotify;
    try {
      notify = FriendAcceptedNotify.decode(body);
    } catch {
      return;
    }
    // Server quirk: the ACCEPTED notify goes to the requester with user_id
    // set to the requester themselves, so the accepting peer's id is not
    // carried. Ignore self echoes (never friend ourselves); the requester's
    // roster gap is closed by the social backend work (P1 backlog).
    if (notify.userId === this.auth.get().userId) return;
    addFriend(this.friends, notify.userId);
  }

  private onFriendRemovedNotify(body: Uint8Array): void {
    let notify: FriendRemovedNotify;
    try {
      notify = FriendRemovedNotify.decode(body);
    } catch {
      return;
    }
    removeFriend(this.friends, notify.userId);
  }
}
