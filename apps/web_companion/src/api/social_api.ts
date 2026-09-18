import { PresenceStatus } from '@chirp/proto/social';
import { MsgID } from '@chirp/proto/gateway';
import {
  ADD_FRIEND,
  BLOCK_USER,
  FRIEND_REQUEST_ACTION,
  GET_FRIEND_LIST,
  GET_PENDING_REQUESTS,
  GET_PRESENCE,
  LOGIN,
  REMOVE_FRIEND,
  SET_PRESENCE,
  UNBLOCK_USER,
} from '../protocol/msg_map';
import { setPresence } from '../state/presence_store';
import {
  addFriend,
  addPendingIn,
  addPendingOut,
  removeFriend,
  replaceFriends,
  replacePendingIn,
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
 * keeps working and the UI simply hides friend features. The backend owns
 * the roster: login pulls friends and pending requests, notifications keep
 * this mirror in sync.
 */
export class SocialApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly presence: Store<PresenceState>;
  private readonly friends: Store<FriendState>;
  private unsubs: Array<() => void> = [];

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
    // Same LOGIN pair as chat (scaffold token = user id); device_id stays
    // unset and lands on the service's "default" device.
    const resp = await this.conn.request(LOGIN, { token: userId, platform: 'web' });
    if (resp.code !== 0) {
      // Degrade quietly: no friend features, chat unaffected.
      this.conn.disconnect();
      return false;
    }
    this.conn.resetBackoff();
    // Reset the local mirror (a previous account's roster must not leak),
    // bring notify handlers up first so nothing racing the pull is lost,
    // then load the authoritative roster.
    this.friends.set(() => ({ friends: [], pendingIn: [], pendingOut: [] }));
    this.start();
    await this.fetchFriends();
    await this.fetchPending();
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

  /** Re-pull the authoritative friend list (e.g. after a reconnect). */
  async fetchFriends(): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(GET_FRIEND_LIST, { userId, limit: 0, offset: 0 });
    if (resp.code !== 0) return resp.code;
    replaceFriends(this.friends, (resp.friends ?? []).map((f) => f.userId));
    return resp.code;
  }

  /** Re-pull the incoming request queue. */
  async fetchPending(): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(GET_PENDING_REQUESTS, { userId });
    if (resp.code !== 0) return resp.code;
    replacePendingIn(
      this.friends,
      (resp.requests ?? []).map((r) => ({ requestId: r.requestId, fromUserId: r.fromUserId })),
    );
    return resp.code;
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
    const resp = await this.conn.request(FRIEND_REQUEST_ACTION, { userId, requestId, accept });
    if (resp.code !== 0) return resp.code;
    resolvePending(this.friends, requestId);
    // Roster sync arrives via the ACCEPTED notify, which the server sends to
    // both sides carrying the OTHER party's id.
    return resp.code;
  }

  /** Symmetric, idempotent delete on the server; the peer learns via notify. */
  async removeFriend(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(REMOVE_FRIEND, { userId, friendUserId: targetUserId });
    if (resp.code === 0) removeFriend(this.friends, targetUserId);
    return resp.code;
  }

  /** Blocking severs the friendship on the server (both ways). */
  async blockUser(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(BLOCK_USER, { userId, targetUserId });
    if (resp.code === 0) removeFriend(this.friends, targetUserId);
    return resp.code;
  }

  async unblockUser(targetUserId: string): Promise<number> {
    const userId = this.auth.get().userId;
    if (!userId) return -1;
    const resp = await this.conn.request(UNBLOCK_USER, { userId, targetUserId });
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
    // user_id is always the OTHER party of the new friendship. A self echo
    // would mean "friend ourselves" — impossible from the current server,
    // but the guard is cheap and keeps a broken peer from poisoning us.
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
