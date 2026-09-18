import { createStore, type Store } from './store';
import type { Conversation } from './models';

export interface ConversationState {
  /** Ordered: most recent activity first. */
  conversations: Conversation[];
}

export const createConversationStore = (): Store<ConversationState> =>
  createStore<ConversationState>({ conversations: [] });

/** Insert or update a conversation and re-sort by recent activity. */
export function upsertConversation(
  store: Store<ConversationState>,
  conversation: Conversation,
): void {
  store.set((prev) => {
    const others = prev.conversations.filter((c) => c.key !== conversation.key);
    const existing = prev.conversations.find((c) => c.key === conversation.key);
    const merged: Conversation = { ...existing, ...conversation, unreadLocal: existing ? existing.unreadLocal : conversation.unreadLocal };
    return { conversations: sortConversations([merged, ...others]) };
  });
}

/** Tail update only (no re-insert for unknown conversations). */
export function touchConversation(
  store: Store<ConversationState>,
  key: string,
  preview: string,
  at: number,
): void {
  store.set((prev) => ({
    conversations: sortConversations(
      prev.conversations.map((c) =>
        c.key === key ? { ...c, lastMessagePreview: preview, lastMessageAt: at } : c,
      ),
    ),
  }));
}

export function bumpUnread(store: Store<ConversationState>, key: string): void {
  store.set((prev) => ({
    conversations: prev.conversations.map((c) =>
      c.key === key ? { ...c, unreadLocal: c.unreadLocal + 1 } : c,
    ),
  }));
}

export function clearUnread(store: Store<ConversationState>, key: string): void {
  store.set((prev) => ({
    conversations: prev.conversations.map((c) =>
      c.key === key ? { ...c, unreadLocal: 0 } : c,
    ),
  }));
}

/** Drop a conversation entirely (left the group, or kicked from it). */
export function removeConversation(store: Store<ConversationState>, key: string): void {
  store.set((prev) => {
    if (!prev.conversations.some((c) => c.key === key)) return prev;
    return { conversations: prev.conversations.filter((c) => c.key !== key) };
  });
}

function sortConversations(conversations: Conversation[]): Conversation[] {
  return [...conversations].sort(
    (a, b) => (b.lastMessageAt ?? 0) - (a.lastMessageAt ?? 0) || a.key.localeCompare(b.key),
  );
}
