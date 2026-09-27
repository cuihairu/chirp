import { createStore, type Store } from './store';

/**
 * 游戏在线状态（P0）：per-account mirror of the game-presence switch.
 * Binding a game opts the account in by default — the server reads an unset
 * switch as enabled, so `enabled` starts true and only an explicit choice
 * flips it. `games` lists the bound game ids whose presence is currently
 * published (empty while the switch is off). Like the device plane, the
 * section hides while the edge is down; chat is unaffected either way.
 */
export interface GamePresenceState {
  /** 游戏在线状态开关；缺省语义为 true（绑定即默认开启）。 */
  enabled: boolean;
  /** GET_GAME_PRESENCE answered at least once: the switch may be shown. */
  loaded: boolean;
  /** Bound games with published presence (server-sorted game ids). */
  games: string[];
  /** Presence plane unreachable: section hides, chat keeps working. */
  unavailable: boolean;
}

export const createGamePresenceStore = (
  initial: Partial<GamePresenceState> = {},
): Store<GamePresenceState> =>
  createStore<GamePresenceState>({
    enabled: true,
    loaded: false,
    games: [],
    unavailable: false,
    ...initial,
  });

/** Stores the merged answer of GET_GAME_PRESENCE (server is authoritative). */
export function applyGamePresence(
  store: Store<GamePresenceState>,
  snapshot: { enabled: boolean; games: string[] },
): void {
  store.set((prev) => ({
    ...prev,
    enabled: snapshot.enabled,
    games: snapshot.games,
    loaded: true,
    unavailable: false,
  }));
}

/** Local write-through of SET_GAME_PRESENCE_ENABLED before the refresh. */
export function setGamePresenceEnabled(
  store: Store<GamePresenceState>,
  enabled: boolean,
): void {
  store.set((prev) => {
    if (prev.enabled === enabled) return prev;
    // Turning the switch off also drops the published games locally; the
    // following refresh confirms against the server.
    return { ...prev, enabled, games: enabled ? prev.games : [] };
  });
}

/** Edge down / logged out: hide the section, forget the mirror. */
export function setGamePresenceUnavailable(
  store: Store<GamePresenceState>,
  unavailable: boolean,
): void {
  store.set((prev) => {
    if (prev.unavailable === unavailable) return prev;
    return unavailable
      ? { ...prev, unavailable, loaded: false, games: [] }
      : { ...prev, unavailable };
  });
}
