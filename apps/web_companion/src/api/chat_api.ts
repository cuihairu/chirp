import {
  ChannelType,
  ChatMessage,
  GroupInfo,
  GroupMemberKickedNotify,
  MessageAck,
  MessageDeletedNotify,
  MessageEditedNotify,
  MessageReadNotify,
  MessageReaction,
  MsgType,
  ReactionAddedNotify,
  ReactionRemovedNotify,
  TypingIndicator,
} from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import type { ConnStatus } from '@chirp/protocol/chirp_client';
import { RequestError } from '@chirp/protocol/errors';
import {
  ADD_REACTION,
  CREATE_GROUP,
  DELETE_MESSAGE,
  EDIT_MESSAGE,
  GET_GROUP_MEMBERS,
  GET_HISTORY,
  GET_USER_GROUPS,
  INVITE_TO_GROUP,
  KICK_MEMBER,
  LEAVE_GROUP,
  LOGIN,
  LOGOUT,
  MARK_READ,
  REMOVE_REACTION,
  SEND_MESSAGE,
  type MessageSpec,
} from '@chirp/protocol/msg_map';
import {
  channelTypeOf,
  conversationOf,
  groupKey,
  privateKey,
  type ChatMessageView,
  type Conversation,
  type MessageReactionView,
} from '../state/models';
import {
  addPendingMessage,
  appendMessage,
  applyDeleteById,
  applyEditById,
  applyReaction,
  clearChannel,
  failPendingMessage,
  prependHistory,
  setReadCursor,
  setLoadingHistory,
  setHasMore,
  setReaction,
  type MessageState,
} from '../state/message_store';
import type { Store } from '../state/store';
import { patch } from '../state/store';
import { bumpUnread, removeConversation, touchConversation, upsertConversation, type ConversationState } from '../state/conversation_store';
import { clearTyping, setTyping, type TypingState } from '../state/typing_store';
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
  typing: Store<TypingState>;
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
  private readonly typing: Store<TypingState>;
  /** The channel currently open on screen; it never accumulates local unread. */
  private activeChannelKey: string | null = null;
  private unsubs: Array<() => void> = [];
  /** UI hooks for live incoming messages (desktop notifications). */
  private messageListeners = new Set<
    (message: { channel: ChannelRef; fromUserId: string; content: string; messageId: string }) => void
  >();

  constructor(deps: ChatApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.conversations = deps.conversations;
    this.messages = deps.messages;
    this.typing = deps.typing;
  }

  /** Subscribe to server pushes. Idempotent; call again after stop(). */
  start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, (body) => this.onChatMessage(body)),
      this.conn.onNotify(MsgID.GROUP_MEMBER_JOINED_NOTIFY, () => void this.refreshGroups()),
      this.conn.onNotify(MsgID.GROUP_MEMBER_LEFT_NOTIFY, () => void this.refreshGroups()),
      this.conn.onNotify(MsgID.GROUP_MEMBER_KICKED_NOTIFY, (body) => this.onKickedNotify(body)),
      this.conn.onNotify(MsgID.GROUP_UPDATED_NOTIFY, () => void this.refreshGroups()),
      this.conn.onNotify(MsgID.MESSAGE_READ_NOTIFY, (body) => this.onReadNotify(body)),
      this.conn.onNotify(MsgID.TYPING_INDICATOR_NOTIFY, (body) => this.onTypingNotify(body)),
      this.conn.onNotify(MsgID.REACTION_ADDED_NOTIFY, (body) => this.onReactionNotify(body, true)),
      this.conn.onNotify(MsgID.REACTION_REMOVED_NOTIFY, (body) => this.onReactionNotify(body, false)),
      this.conn.onNotify(MsgID.MESSAGE_EDITED_NOTIFY, (body) => this.onEditedNotify(body)),
      this.conn.onNotify(MsgID.MESSAGE_DELETED_NOTIFY, (body) => this.onDeletedNotify(body)),
    ];
  }

  stop(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  /**
   * Fires for every live CHAT_MESSAGE_NOTIFY after it lands in the stores —
   * used by the desktop-notification surface. Returns the unsubscribe fn.
   */
  onMessage(
    listener: (message: {
      channel: ChannelRef;
      fromUserId: string;
      content: string;
      messageId: string;
    }) => void,
  ): () => void {
    this.messageListeners.add(listener);
    return () => this.messageListeners.delete(listener);
  }

  setActiveChannel(key: string | null): void {
    this.activeChannelKey = key;
  }

  async login(userId: string): Promise<ErrorCode> {
    // First entry (or a dead socket) must open the connection; already-open
    // sockets are left alone. The fake resolves instantly, the real client
    // goes idle → connecting → connected.
    if (this.conn.status !== 'connected') {
      await this.conn.connect();
    }
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

  /** Typing is fire-and-forget (2208, no RESP); the UI does the throttling. */
  sendTyping(channel: ChannelRef, isTyping: boolean): void {
    const userId = this.auth.get().userId;
    if (!userId) return;
    this.conn.send(
      MsgID.TYPING_INDICATOR_NOTIFY,
      TypingIndicator.encode(
        TypingIndicator.fromPartial({
          channelId: channel.channelId,
          channelType: channelTypeOf(channel.kind),
          userId,
          username: userId,
          isTyping,
          timestamp: Date.now(),
        }),
      ).finish(),
    );
  }

  /** Toggle own emoji on a message; the RESP aggregate is the truth. */
  async addReaction(messageId: string, emoji: string): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(ADD_REACTION, { messageId, userId, emoji });
    if (resp.code !== 0 || !resp.reaction) return;
    setReaction(this.messages, messageId, toReactionView(resp.reaction, userId));
  }

  async removeReaction(messageId: string, emoji: string): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(REMOVE_REACTION, { messageId, userId, emoji });
    if (resp.code !== 0) return;
    // REMOVE_REACTION_RESP carries no aggregate; the notify is excluded for
    // the actor, so decrement locally.
    applyReaction(this.messages, messageId, emoji, true, false);
  }

  /** Edits apply locally from the RESP: the notify excludes the editor. */
  async editMessage(messageId: string, text: string): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(EDIT_MESSAGE, {
      messageId,
      userId,
      newContent: new TextEncoder().encode(text),
      editTimestamp: Date.now(),
    });
    if (resp.code !== 0) return;
    applyEditById(this.messages, messageId, text);
  }

  /** Soft delete (is_hard_delete is admin-only server-side). */
  async deleteMessage(messageId: string): Promise<void> {
    const userId = this.auth.get().userId;
    if (!userId) return;
    const resp = await this.conn.request(DELETE_MESSAGE, {
      messageId,
      userId,
      isHardDelete: false,
    });
    if (resp.code !== 0) return;
    applyDeleteById(this.messages, messageId);
  }

  /** Group roster for the conversation list (title side-info and badges). */
  async refreshGroups(): Promise<Conversation[]> {
    const userId = this.auth.get().userId;
    if (!userId) return [];
    const resp = await this.conn.request(GET_USER_GROUPS, { userId });
    if (resp.code !== 0) return [];
    const existing = this.conversations.get().conversations;
    const conversations = (resp.groups ?? []).map((group: GroupInfo) => {
      const key = groupKey(group.groupId);
      const prev = existing.find((c) => c.key === key);
      return (
        prev ?? {
          kind: 'group' as const,
          key,
          channelId: group.groupId,
          peerId: group.groupId,
          title: group.groupName,
          ownerId: group.ownerId,
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

  /** INVITE adds directly server-side (no pending state) — "添加成员". */
  async inviteToGroup(groupId: string, targetUserId: string): Promise<ErrorCode> {
    const userId = this.auth.get().userId;
    if (!userId) return ErrorCode.SESSION_EXPIRED;
    const resp = await this.conn.request(INVITE_TO_GROUP, {
      inviterId: userId,
      groupId,
      targetUserId,
    });
    if (resp.code === 0) await this.refreshGroups();
    return resp.code;
  }

  async kickMember(groupId: string, targetUserId: string): Promise<ErrorCode> {
    const userId = this.auth.get().userId;
    if (!userId) return ErrorCode.SESSION_EXPIRED;
    const resp = await this.conn.request(KICK_MEMBER, {
      requesterId: userId,
      groupId,
      targetUserId,
    });
    if (resp.code === 0) await this.refreshGroups();
    return resp.code;
  }

  /** Leave and drop the local cache: the MEMBER_LEFT notify excludes the actor. */
  async leaveGroup(groupId: string): Promise<ErrorCode> {
    const userId = this.auth.get().userId;
    if (!userId) return ErrorCode.SESSION_EXPIRED;
    const resp = await this.conn.request(LEAVE_GROUP, { userId, groupId });
    if (resp.code === 0) {
      const key = groupKey(groupId);
      removeConversation(this.conversations, key);
      clearChannel(this.messages, key);
      if (this.activeChannelKey === key) this.activeChannelKey = null;
    }
    return resp.code;
  }

  /** 2120: someone was kicked. If it was me, forget the group locally. */
  private onKickedNotify(body: Uint8Array): void {
    let notify: GroupMemberKickedNotify;
    try {
      notify = GroupMemberKickedNotify.decode(body);
    } catch {
      return;
    }
    const key = groupKey(notify.groupId);
    if (notify.userId === this.auth.get().userId) {
      removeConversation(this.conversations, key);
      clearChannel(this.messages, key);
      if (this.activeChannelKey === key) this.activeChannelKey = null;
    } else {
      void this.refreshGroups();
    }
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
    // We logged in with supportsMessageAck=true, so every live push must be
    // acked or the server rolls the delivery back into the offline queue
    // after 10s. Fire-and-forget (2209 has no response frame).
    this.conn.send(
      MsgID.MESSAGE_ACK,
      MessageAck.encode(
        MessageAck.fromPartial({
          messageId: msg.messageId,
          userId: this.auth.get().userId ?? '',
          receivedAt: Date.now(),
        }),
      ).finish(),
    );
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
    const content = decodeText(msg.content);
    for (const listener of this.messageListeners) {
      listener({
        channel,
        fromUserId: msg.senderId,
        content,
        messageId: msg.messageId,
      });
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

  private onReadNotify(body: Uint8Array): void {
    let notify: MessageReadNotify;
    try {
      notify = MessageReadNotify.decode(body);
    } catch {
      return;
    }
    const selfId = this.auth.get().userId ?? '';
    if (notify.readerUserId === selfId) return; // own echoes are meaningless
    const key =
      notify.channelType === ChannelType.PRIVATE
        ? privateKey(notify.readerUserId, selfId)
        : groupKey(notify.channelId);
    setReadCursor(this.messages, key, notify.readerUserId, notify.messageId);
  }

  private onTypingNotify(body: Uint8Array): void {
    let indicator: TypingIndicator;
    try {
      indicator = TypingIndicator.decode(body);
    } catch {
      return;
    }
    const selfId = this.auth.get().userId ?? '';
    if (indicator.userId === selfId) return;
    const key =
      indicator.channelType === ChannelType.PRIVATE
        ? privateKey(indicator.userId, selfId)
        : groupKey(indicator.channelId);
    if (indicator.isTyping) setTyping(this.typing, key, indicator.userId, Date.now());
    else clearTyping(this.typing, key, indicator.userId);
  }

  private onReactionNotify(body: Uint8Array, added: boolean): void {
    let notify: ReactionAddedNotify | ReactionRemovedNotify;
    try {
      notify = added
        ? ReactionAddedNotify.decode(body)
        : ReactionRemovedNotify.decode(body);
    } catch {
      return;
    }
    applyReaction(this.messages, notify.messageId, notify.emoji, notify.userId === this.auth.get().userId, added);
  }

  private onEditedNotify(body: Uint8Array): void {
    let notify: MessageEditedNotify;
    try {
      notify = MessageEditedNotify.decode(body);
    } catch {
      return;
    }
    applyEditById(this.messages, notify.messageId, decodeText(notify.newContent));
  }

  private onDeletedNotify(body: Uint8Array): void {
    let notify: MessageDeletedNotify;
    try {
      notify = MessageDeletedNotify.decode(body);
    } catch {
      return;
    }
    applyDeleteById(this.messages, notify.messageId);
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

const toReactionView = (reaction: MessageReaction, selfId: string): MessageReactionView => ({
  emoji: reaction.emoji,
  count: reaction.count,
  mine: reaction.reactedByMe || reaction.userIds.includes(selfId),
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
