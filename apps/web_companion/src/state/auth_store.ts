import { createStore, type Store } from './store';

export interface AuthState {
  /** Logged-in user id; null on the login screen. */
  userId: string | null;
  /** Server sent KICK_NOTIFY: session taken over by another device. */
  kicked: boolean;
  /** device_id persisted per browser so reconnects never self-kick. */
  deviceId: string;
  /** Set once a LOGIN round-trip has succeeded. */
  loggedIn: boolean;
}

export const createAuthStore = (): Store<AuthState> =>
  createStore<AuthState>({
    userId: null,
    kicked: false,
    deviceId: ensureDeviceId(),
    loggedIn: false,
  });

const DEVICE_ID_STORAGE = 'chirp.device_id';

/**
 * Random per-browser device id. The server kicks the previous session of the
 * same (user, device) pair, so a stable id means an ordinary reconnect never
 * logs the user out; a second browser tab gets its own id and may take over.
 */
export function ensureDeviceId(): string {
  try {
    const existing = window.localStorage.getItem(DEVICE_ID_STORAGE);
    if (existing) return existing;
    const fresh =
      globalThis.crypto?.randomUUID?.().slice(0, 8) ?? Math.random().toString(36).slice(2, 10);
    const id = `web-${fresh}`;
    window.localStorage.setItem(DEVICE_ID_STORAGE, id);
    return id;
  } catch {
    // Storage can be unavailable (private mode); a per-load id still works.
    return 'web-ephemeral';
  }
}
