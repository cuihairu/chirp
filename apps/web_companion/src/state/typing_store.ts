import { createStore, type Store } from './store';

/**
 * Typing indicators, per channel per user, timestamped on arrival. There is
 * no server-side stop guarantee beyond the explicit stop event, so the UI
 * treats entries older than TTL as idle (the server also broadcasts the stop
 * and rate-limits starts to one per ~3s).
 */
export const TYPING_TTL_MS = 6000;

export interface TypingState {
  /** channelKey → userId → last event timestamp (ms, client clock). */
  byChannel: Record<string, Record<string, number>>;
}

export const createTypingStore = (): Store<TypingState> =>
  createStore<TypingState>({ byChannel: {} });

export function setTyping(
  store: Store<TypingState>,
  channelKey: string,
  userId: string,
  at: number,
): void {
  store.set((prev) => ({
    byChannel: {
      ...prev.byChannel,
      [channelKey]: { ...(prev.byChannel[channelKey] ?? {}), [userId]: at },
    },
  }));
}

export function clearTyping(store: Store<TypingState>, channelKey: string, userId: string): void {
  store.set((prev) => {
    const channel = { ...(prev.byChannel[channelKey] ?? {}) };
    if (!(userId in channel)) return prev;
    delete channel[userId];
    return { byChannel: { ...prev.byChannel, [channelKey]: channel } };
  });
}

/** Users seen typing in this channel within the TTL (self already excluded). */
export function typingUsersOf(
  state: TypingState,
  channelKey: string,
  now: number,
): string[] {
  return Object.entries(state.byChannel[channelKey] ?? {})
    .filter(([, at]) => now - at < TYPING_TTL_MS)
    .map(([userId]) => userId);
}
