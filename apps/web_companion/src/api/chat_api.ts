import {
  ChannelType,
  ChatMessage,
  GroupInfo,
  MsgType,
} from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import type { ConnStatus } from '../protocol/chirp_client';
import { RequestError } from '../protocol/errors';
import {
  CREATE_GROUP,
  GET_GROUP_MEMBERS,
  GET_HISTORY,
  GET_USER_GROUPS,
  LOGIN,
  LOGOUT,
  MARK_READ,
  SEND_MESSAGE,
  type MessageSpec,
} from '../protocol/msg_map';
import {
  channelTypeOf,
  conversationOf,
  groupKey,
  privateKey,
  type ChatMessageView,
  type Conversation,
} from '../state/models';
import {
  addPendingMessage,
  appendMessage,
  failPendingMessage,
  prependHistory,
  setLoadingHistory,
  setHasMore,
  type MessageState,
} from '../state/message_store';
import type { Store } from '../state/store';
import { patch } from '../state/store';
import { bumpUnread, touchConversation, upsertConversation, type ConversationState } from '../state/conversation_store';
import type { AuthState } from '../state/auth_store';

/**
 * Structural subset of ChirpClient so tests can inject a fake; the real
 * client satisfies it as-is.
 */
export interface ChatConnection {
  connect(): Promise<void>;
  disconnect(): void;
  resetBackoff(): void;
  request<Req, Resp>(
    spec: MessageSpec<Req, Resp>,
    req: Partial<Req>,
    timeoutMs?: number,
  ): Promise<Resp>;
  send(msgId: MsgID, body: Uint8Array): void;
  onNotify(msgId: MsgID, handler: (body: Uint8Array) => void): () => void;
  onStatus(listener: (status: ConnStatus) => void): () => void;
  heartbeatNow(): void;
  readonly status: ConnStatus;
  readonly kicked: boolean;
}

export interface ChatApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  conversations: Store<ConversationState>;
  messages: Store<MessageState>;
}

export interface ChannelRef {
  key: string;
  kind: 'private' | 'group';
  channelId: string;
  peerId: string;
}

let pendingCounter = 0;

/**
 * Chat feature API: owns the notify→store wiring and turns request/response
 * pairs into UI mutations. Deliberately framework-free.
 */
export class ChatApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly conversations: Store<ConversationState>;
  private readonly messages: Store<MessageState>;
  /** The channel currently open on screen; it never accumulates local unread. */
  private activeChannelKey: string | null = null;
  private unsubs: Array<() => void> = [];

  constructor(deps: ChatApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.conversations = deps.conversations;
    this.messages = deps.messages;
  }

  /** Subscribe to server pushes. Idempotent; call again after stop(). */
  start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, (body) => this.onChatMessage(body)),
      this.conn.onNotify(MsgID.GROUP_MEMBER_JOINED_NOTIFY, () => this.refreshGroups()),
      this.conn.onNotify(MsgID.GROUP_MEMBER_LEFT_NOTIFY, () => this.refreshGroups()),
    ];
  }

  stop(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  setActiveChannel(key: string | null): void {
    this.activeChannelKey = key;
  }

  async login(userId: string): Promise<ErrorCode> {
    const resp = await this.conn.request(LOGIN, {
      token: userId,
      deviceId: this.auth.get().deviceId,
      platform: 'web',
      supportsMessageAck: true,
    });
    if (resp.code === 0) {
      patch(this.auth, { userId, loggedIn: true, kicked: false });
      this.conn.resetBackoff();
      this.start();
    }
    return resp.code;
  }

  /** Server-side logout, then a plain disconnect. */
  async logout(): Promise<void> {
    const userId = this.auth.get().userId;
    try {
      if (userId) {
        await this.conn.request(LOGOUT, { userId });
      }
    } catch {
      // The disconnect below is what matters; a failed LOGOUT is harmless.
    }
    this.stop();
    this.conn.disconnect();
    patch(this.auth, { userId: null, loggedIn: false, kicked: false });
  }

  /**
   * Optimistic send: the message goes on screen immediately and is replaced
   * (or failed) by the SEND_MESSAGE_RESP. TARGET_OFFLINE is NOT a failure —
   * the server already queued the message for the recipient.
   */
  async sendMessage(channel: ChannelRef, text: string): Promise<void> {
    const senderId = this.auth.get().userId;
    if (!senderId) return;
    const clientId = `pending-${Date.now().toString(36)}-${pendingCounter++}`;
    const optimistic: ChatMessageView = {
      messageId: clientId,
      clientId,
      senderId,
      channelKey: channel.key,
      channelType: channelTypeOf(channel.kind),
      channelId: channel.channelId,
      content: text,
      timestamp: Date.now(),
      pending: true,
    };
    addPendingMessage(this.messages, optimistic);

    let resp;
    try {
      resp = await this.conn.request(SEND_MESSAGE, {
        senderId,
        // Group sends MUST leave receiver empty (chat_validation.cc rejects it).
        receiverId: channel.kind === 'private' ? channel.peerId : '',
        channelType: channelTypeOf(channel.kind),
        channelId: channel.channelId,
        msgType: MsgType.TEXT,
        content: new TextEncoder().encode(text),
        clientTimestamp: Date.now(),
      });
    } catch (err) {
      if (!(err instanceof RequestError)) throw err;
      failPendingMessage(this.messages, channel.key, clientId);
      return;
    }
    if (resp.code === 0 || resp.code === ErrorCode.TARGET_OFFLINE) {
      const confirmed: ChatMessageView = {
        ...optimistic,
        messageId: resp.messageId || clientId,
        pending: false,
        queuedOffline: resp.code === ErrorCode.TARGET_OFFLINE,
      };
      appendMessage(this.messages, confirmed);
      // First send to a fresh peer/group: the tail shows up in the list here.
      this.ensureConversation(channel, channel.kind === 'private' ? channel.peerId : channel.channelId);
      touchConversation(this.conversations, channel.key, text, confirmed.timestamp);
    } else {
      failPendingMessage(this.messages, channel.key, clientId);
    }
  }

  /** Load (or page) history; oldest first, newest last. */
  async loadHistory(channel: ChannelRef, beforeTimestamp?: number): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    setLoadingHistory(this.messages, channel.key, true);
    try {
      const resp = await this.conn.request(GET_HISTORY, {
        userId,
        channelType: channelTypeOf(channel.kind),
        channelId: channel.channelId,
        limit: 50,
        beforeTimestamp: beforeTimestamp ?? 0,
      });
      if (resp.code !== 0) return;
      setHasMore(this.messages, channel.key, resp.hasMore);
      prependHistory(
        this.messages,
        channel.key,
        resp.messages.map((m) => toView(m, channel)),
        resp.hasMore,
      );
    } finally {
      setLoadingHistory(this.messages, channel.key, false);
    }
  }

  async markRead(channel: ChannelRef, messageId = ''): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    try {
      await this.conn.request(MARK_READ, {
        userId,
        channelType: channelTypeOf(channel.kind),
        channelId: channel.channelId,
        messageId,
        readTimestamp: Date.now(),
      });
    } catch {
      // Receipts are best-effort; the local unread badge already cleared.
    }
  }

  /** Group roster for the conversation list (title side-info and badges). */
  async refreshGroups(): Promise<Conversation[]> {
    const userId = this.auth.get().userId;
    if (!userId) return [];
    const resp = await this.conn.request(GET_USER_GROUPS, { userId });
    if (resp.code !== 0) return [];
    const existing = this.conversations.get().conversations;
    const conversations = resp.groups.map((group: GroupInfo) => {
      const key = groupKey(group.groupId);
      const prev = existing.find((c) => c.key === key);
      return (
        prev ?? {
          kind: 'group' as const,
          key,
          channelId: group.groupId,
          peerId: group.groupId,
          title: group.groupName,
          unreadLocal: 0,
        }
      );
    });
    for (const conversation of conversations) {
      upsertConversation(this.conversations, conversation);
    }
    return conversations;
  }

  async loadGroupMembers(groupId: string) {
    const resp = await this.conn.request(GET_GROUP_MEMBERS, { groupId });
    return resp.code === 0 ? resp.members : [];
  }

  async createGroup(name: string, description = ''): Promise<string | null> {
    const userId = this.auth.get().userId;
    if (!userId) return null;
    const resp = await this.conn.request(CREATE_GROUP, { creatorId: userId, groupName: name, description });
    if (resp.code !== 0) return null;
    await this.refreshGroups();
    return resp.groupId;
  }

  private onChatMessage(body: Uint8Array): void {
    let msg: ChatMessage;
    try {
      msg = ChatMessage.decode(body);
    } catch {
      return;
    }
    const key =
      msg.channelType === ChannelType.PRIVATE
        ? privateKey(msg.senderId, this.auth.get().userId ?? '')
        : groupKey(msg.channelId);
    const channel: ChannelRef = {
      key,
      kind: msg.channelType === ChannelType.PRIVATE ? 'private' : 'group',
      channelId: msg.channelId,
      peerId: msg.channelType === ChannelType.PRIVATE ? msg.senderId : msg.channelId,
    };
    appendMessage(this.messages, toView(msg, channel));
    this.ensureConversation(
      channel,
      channel.kind === 'private' ? msg.senderId : msg.channelId,
    );
    touchConversation(this.conversations, key, decodeText(msg.content), msg.timestamp);
    if (key !== this.activeChannelKey) {
      bumpUnread(this.conversations, key);
    }
  }

  /**
   * A private message from someone we never talked to joins the list here:
   * the server has no private-conversation listing API (P1 aggregation is
   * the authoritative fix), so the client fills the gap locally.
   */
  private ensureConversation(channel: ChannelRef, title: string): void {
    const exists = this.conversations.get().conversations.some((c) => c.key === channel.key);
    if (!exists) {
      upsertConversation(this.conversations, {
        kind: channel.kind,
        key: channel.key,
        channelId: channel.channelId,
        peerId: channel.peerId,
        title,
        unreadLocal: 0,
      });
    }
  }
}

const decodeText = (bytes: Uint8Array): string => new TextDecoder().decode(bytes);

const toView = (msg: ChatMessage, channel: ChannelRef): ChatMessageView => ({
  messageId: msg.messageId,
  senderId: msg.senderId,
  channelKey: channel.key,
  channelType: msg.channelType,
  channelId: msg.channelId,
  content: decodeText(msg.content),
  timestamp: msg.timestamp,
  pending: false,
});

/** Build a ChatConnection from a real client (marker for the wiring site). */
export const asConnection = (client: ChatConnection): ChatConnection => client;

/** ChannelRef for a stored conversation key; private peers resolve via privatePeerId. */
export const channelRefOf = (key: string, selfId: string): ChannelRef => {
  const { kind, channelId } = conversationOf(key);
  return {
    key,
    kind,
    channelId,
    peerId: kind === 'group' ? channelId : privatePeerId(channelId, selfId),
  };
};

/** Private peer id from a sorted-pair channel id ("a|b" → the other side). */
export const privatePeerId = (channelId: string, selfId: string): string =>
  channelId.split('|').find((id) => id !== selfId) ?? channelId;
