import { createStore, type Store } from './store';
import type { ChatMessageView } from './models';

export interface MessageState {
  /** channelKey → messages ordered oldest → newest. */
  byChannel: Record<string, ChatMessageView[]>;
  /** Older history exists on the server for this channel. */
  hasMore: Record<string, boolean>;
  loadingHistory: Record<string, boolean>;
}

export const createMessageStore = (): Store<MessageState> =>
  createStore<MessageState>({ byChannel: {}, hasMore: {}, loadingHistory: {} });

export const messagesOf = (state: MessageState, key: string): ChatMessageView[] =>
  state.byChannel[key] ?? [];

/**
 * Append live/confirmed messages and drop duplicates by server id. The
 * sender gets no echo of its own messages: its copies arrive through
 * SEND_MESSAGE_RESP and are matched by clientId first.
 */
export function appendMessage(store: Store<MessageState>, message: ChatMessageView): void {
  store.set((prev) => {
    const list = prev.byChannel[message.channelKey] ?? [];
    if (list.some((m) => m.messageId === message.messageId)) return prev;
    // An optimistic placeholder for the same message is replaced, not added;
    // without a match the message is appended to the tail.
    const replaced = message.clientId
      ? list.map((m) => (m.clientId === message.clientId ? message : m))
      : list;
    return withChannel(
      prev,
      message.channelKey,
      replaced === list ? [...list, message] : replaced,
    );
  });
}

/** Upsert the optimistic placeholder for a send awaiting its RESP. */
export function addPendingMessage(store: Store<MessageState>, message: ChatMessageView): void {
  store.set((prev) => {
    const list = prev.byChannel[message.channelKey] ?? [];
    return withChannel(prev, message.channelKey, [...list, message]);
  });
}

/** Flip a pending send to failed (RESP error or timeout). */
export function failPendingMessage(
  store: Store<MessageState>,
  channelKey: string,
  clientId: string,
): void {
  store.set((prev) => ({
    ...prev,
    byChannel: {
      ...prev.byChannel,
      [channelKey]: (prev.byChannel[channelKey] ?? []).map((m) =>
        m.clientId === clientId ? { ...m, pending: false, failed: true } : m,
      ),
    },
  }));
}

/** Prepend a page of older history; `hasMore` comes from the server. */
export function prependHistory(
  store: Store<MessageState>,
  channelKey: string,
  messages: ChatMessageView[],
  hasMore: boolean,
): void {
  store.set((prev) => {
    const existing = prev.byChannel[channelKey] ?? [];
    const known = new Set(existing.map((m) => m.messageId));
    const fresh = messages.filter((m) => !known.has(m.messageId));
    return {
      ...prev,
      byChannel: {
        ...prev.byChannel,
        [channelKey]: [...fresh, ...existing],
      },
      hasMore: { ...prev.hasMore, [channelKey]: hasMore },
    };
  });
}

export function setLoadingHistory(
  store: Store<MessageState>,
  channelKey: string,
  loading: boolean,
): void {
  store.set((prev) => ({
    ...prev,
    loadingHistory: { ...prev.loadingHistory, [channelKey]: loading },
  }));
}

export function setHasMore(store: Store<MessageState>, channelKey: string, hasMore: boolean): void {
  store.set((prev) => ({ ...prev, hasMore: { ...prev.hasMore, [channelKey]: hasMore } }));
}

export function applyEdit(
  store: Store<MessageState>,
  channelKey: string,
  messageId: string,
  content: string,
): void {
  store.set((prev) => ({
    ...prev,
    byChannel: {
      ...prev.byChannel,
      [channelKey]: (prev.byChannel[channelKey] ?? []).map((m) =>
        m.messageId === messageId ? { ...m, content, edited: true } : m,
      ),
    },
  }));
}

export function applyDelete(store: Store<MessageState>, channelKey: string, messageId: string): void {
  store.set((prev) => ({
    ...prev,
    byChannel: {
      ...prev.byChannel,
      [channelKey]: (prev.byChannel[channelKey] ?? []).map((m) =>
        m.messageId === messageId ? { ...m, deleted: true, content: '' } : m,
      ),
    },
  }));
}

function withChannel(
  prev: MessageState,
  channelKey: string,
  list: ChatMessageView[],
): MessageState {
  return { ...prev, byChannel: { ...prev.byChannel, [channelKey]: list } };
}
