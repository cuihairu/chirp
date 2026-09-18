import { ChannelType } from '@chirp/proto/chat';

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
  /** TARGET_OFFLINE is still a delivery: queued server-side. */
  queuedOffline?: boolean;
  edited?: boolean;
  deleted?: boolean;
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
