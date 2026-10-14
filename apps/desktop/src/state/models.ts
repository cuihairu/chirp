import { ChannelType, Priority, SenderKind } from '@chirp/proto/chat';

/**
 * UI-layer models. Channel keys are local-only: the server keys private
 * history by the sorted "a|b" pair and groups by group id, and the app needs
 * one namespace for conversations across both.
 */

export type ConversationKind = 'private' | 'group';

export interface Conversation {
  kind: ConversationKind;
  /** 'p:' + sorted pair for private, 'g:' + group id for groups. */
  key: string;
  /** Server-side channel id ('a|b' or group id). */
  channelId: string;
  /** Peer user id (private) or group id (group). */
  peerId: string;
  title: string;
  /** Group owner (groups only) — gates the kick affordance in the UI. */
  ownerId?: string;
  /** Tail of the conversation, kept in sync by message_store. */
  lastMessagePreview?: string;
  lastMessageAt?: number;
  /** Local unread badge count; bumps on notifies outside the open channel. */
  unreadLocal: number;
}

export interface MessageReactionView {
  /** Unicode emoji. */
  emoji: string;
  count: number;
  /** Whether the logged-in user is among the reactors. */
  mine: boolean;
}

/** Coordinate card (P1 COORD_LINK): tap to copy "x, y[, z]". */
export interface CoordView {
  mapId: string;
  x: number;
  y: number;
  z: number;
  label: string;
}

/** Item link card (P0 ITEM_LINK): tap to open the detail dialog. */
export interface ItemView {
  itemId: string;
  itemName: string;
  /** 品质 0 白/1 绿/2 蓝/3 紫/4 橙；枚举外未知值按白渲染（向前兼容）。 */
  quality: number;
  iconUrl: string;
  count: number;
  attrs: Record<string, string>;
}

/** Achievement share card (P1 ACHIEVEMENT): icon + name + rarity + description. */
export interface AchievementView {
  achievementId: string;
  achievementName: string;
  description: string;
  iconUrl: string;
  /** 稀有度 0 普通/1 优秀/2 稀有/3 史诗/4 传说；枚举外未知值按普通渲染。 */
  rarity: number;
}

export interface ChatMessageView {
  /** Server message id; optimistic sends carry a local id until confirmed. */
  messageId: string;
  /** Present while the message is an optimistic placeholder. */
  clientId?: string;
  senderId: string;
  channelKey: string;
  channelType: ChannelType;
  channelId: string;
  /** Decoded text (phase one is TEXT-only). */
  content: string;
  timestamp: number;
  /** Optimistic, not yet acknowledged by a SEND_MESSAGE_RESP. */
  pending: boolean;
  /** Server rejected the send (RESP code != OK). */
  failed?: boolean;
  /** Rejection reason detail: 'muted' surfaces the mute-specific bubble hint. */
  failureReason?: 'muted';
  /** TARGET_OFFLINE is still a delivery: queued server-side. */
  queuedOffline?: boolean;
  edited?: boolean;
  deleted?: boolean;
  /** Quoted parent message id (P1 reply); absent = not a reply. */
  replyToMessageId?: string;
  /** Coordinate card (P1 COORD_LINK); absent = plain text bubble. */
  coord?: CoordView;
  /** Item link card (P0 ITEM_LINK); absent = plain text bubble. */
  item?: ItemView;
  /** Achievement share card (P1 ACHIEVEMENT); absent = plain text bubble. */
  achievement?: AchievementView;
  // P0 消息展示：wire 三字段透传（ChatMessage priority/ttl_seconds/sender_kind）。
  // 缺省（proto3 零值）= 普通玩家、普通优先级、无走马灯时长。
  priority?: Priority;
  ttlSeconds?: number;
  senderKind?: SenderKind;
  /** Emoji → aggregate, updated by reaction RESP/notify. */
  reactions?: Record<string, MessageReactionView>;
}

export const PRIVATE_PREFIX = 'p:';
export const GROUP_PREFIX = 'g:';

export const privateKey = (a: string, b: string): string =>
  PRIVATE_PREFIX + [a, b].sort().join('|');

export const groupKey = (groupId: string): string => GROUP_PREFIX + groupId;

export function conversationOf(key: string): { kind: ConversationKind; channelId: string } {
  if (key.startsWith(PRIVATE_PREFIX)) {
    return { kind: 'private', channelId: key.slice(PRIVATE_PREFIX.length) };
  }
  return { kind: 'group', channelId: key.slice(GROUP_PREFIX.length) };
}

export const channelTypeOf = (kind: ConversationKind): ChannelType =>
  kind === 'private' ? ChannelType.PRIVATE : ChannelType.GUILD;
