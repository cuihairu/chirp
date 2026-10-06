import { createStore, type Store } from './store';

export interface AuthState {
  /** Logged-in user id; null on the login screen. */
  userId: string | null;
  /** Server sent KICK_NOTIFY: session taken over by another device. */
  kicked: boolean;
  /** device_id persisted per install so reconnects never self-kick. */
  deviceId: string;
  /** Set once a LOGIN round-trip has succeeded. */
  loggedIn: boolean;
}

/**
 * Ported from apps/web_companion src/state/auth_store.ts. One adaptation:
 * the browser port reads localStorage on construction, which does not exist
 * on this side — the persisted device id is handed in by the platform layer
 * (HostConfig, @ohos.data.preferences) after preload, so the store itself
 * stays storage-free and the semantics (stable id per install) match.
 */
export const createAuthStore = (deviceId: string): Store<AuthState> =>
  createStore<AuthState>({
    userId: null,
    kicked: false,
    deviceId,
    loggedIn: false,
  });
