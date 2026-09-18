import { createStore, type Store } from './store';
import type { ChatMessageView, MessageReactionView } from './models';

export interface MessageState {
  /** channelKey → messages ordered oldest → newest. */
  byChannel: Record<string, ChatMessageView[]>;
  /** Older history exists on the server for this channel. */
  hasMore: Record<string, boolean>;
  loadingHistory: Record<string, boolean>;
  /** channelKey → userId → last message id that user has read. */
  readCursors: Record<string, Record<string, string>>;
}

export const createMessageStore = (): Store<MessageState> =>
  createStore<MessageState>({ byChannel: {}, hasMore: {}, loadingHistory: {}, readCursors: {} });

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

/** Record a MESSAGE_READ_NOTIFY: readerUserId has read up to messageId. */
export function setReadCursor(
  store: Store<MessageState>,
  channelKey: string,
  readerUserId: string,
  messageId: string,
): void {
  store.set((prev) => {
    const channel = prev.readCursors[channelKey] ?? {};
    if (channel[readerUserId] === messageId) return prev;
    return {
      ...prev,
      readCursors: {
        ...prev.readCursors,
        [channelKey]: { ...channel, [readerUserId]: messageId },
      },
    };
  });
}

export const readCursorOf = (
  state: MessageState,
  channelKey: string,
  readerUserId: string,
): string | undefined => state.readCursors[channelKey]?.[readerUserId];

/**
 * Apply a reaction RESP/notify by locating the message id across channels
 * (message ids are globally unique; the notify carries no channel type, so
 * scanning is the honest option at companion scale).
 */
export function applyReaction(
  store: Store<MessageState>,
  messageId: string,
  emoji: string,
  mine: boolean,
  added: boolean,
): void {
  store.set((prev) => {
    let changed = false;
    const byChannel: Record<string, ChatMessageView[]> = {};
    for (const [key, list] of Object.entries(prev.byChannel)) {
      byChannel[key] = list.map((m) => {
        if (m.messageId !== messageId) return m;
        changed = true;
        const reactions = { ...(m.reactions ?? {}) };
        const current = reactions[emoji] ?? { emoji, count: 0, mine: false };
        let mineNow = current.mine;
        let count = current.count;
        if (added) {
          count += 1;
          if (mine) mineNow = true;
        } else {
          count = Math.max(0, count - 1);
          if (mine) mineNow = false;
        }
        if (count === 0) delete reactions[emoji];
        else reactions[emoji] = { emoji, count, mine: mineNow };
        return { ...m, reactions: Object.keys(reactions).length ? reactions : undefined };
      });
    }
    return changed ? { ...prev, byChannel } : prev;
  });
}

/** Replace a reaction aggregate wholesale (from ADD_REACTION_RESP). */
export function setReaction(
  store: Store<MessageState>,
  messageId: string,
  reaction: MessageReactionView,
): void {
  store.set((prev) => {
    let changed = false;
    const byChannel: Record<string, ChatMessageView[]> = {};
    for (const [key, list] of Object.entries(prev.byChannel)) {
      byChannel[key] = list.map((m) => {
        if (m.messageId !== messageId) return m;
        changed = true;
        const reactions = { ...(m.reactions ?? {}), [reaction.emoji]: reaction };
        return { ...m, reactions };
      });
    }
    return changed ? { ...prev, byChannel } : prev;
  });
}

/** Edit/delete by message id, wherever it lives (notifies carry no channel type). */
export function applyEditById(
  store: Store<MessageState>,
  messageId: string,
  content: string,
): void {
  store.set((prev) => {
    let changed = false;
    const byChannel: Record<string, ChatMessageView[]> = {};
    for (const [key, list] of Object.entries(prev.byChannel)) {
      byChannel[key] = list.map((m) => {
        if (m.messageId !== messageId) return m;
        changed = true;
        return { ...m, content, edited: true };
      });
    }
    return changed ? { ...prev, byChannel } : prev;
  });
}

export function applyDeleteById(store: Store<MessageState>, messageId: string): void {
  store.set((prev) => {
    let changed = false;
    const byChannel: Record<string, ChatMessageView[]> = {};
    for (const [key, list] of Object.entries(prev.byChannel)) {
      byChannel[key] = list.map((m) => {
        if (m.messageId !== messageId) return m;
        changed = true;
        return { ...m, deleted: true, content: '' };
      });
    }
    return changed ? { ...prev, byChannel } : prev;
  });
}

function withChannel(
  prev: MessageState,
  channelKey: string,
  list: ChatMessageView[],
): MessageState {
  return { ...prev, byChannel: { ...prev.byChannel, [channelKey]: list } };
}
