import { GET_GAME_PRESENCE, SET_GAME_PRESENCE_ENABLED } from '@chirp/protocol/msg_map';
import type { GamePresenceEntry } from '@chirp/proto/game_server_gateway';
import type { Store } from '../state/store';
import type { AuthState } from '../state/auth_store';
import {
  applyGamePresence,
  setGamePresenceEnabled,
  setGamePresenceUnavailable,
  type GamePresenceState,
} from '../state/game_presence_store';
import type { ChatConnection } from './chat_api';

export interface GamePresenceApiDeps {
  conn: ChatConnection;
  auth: Store<AuthState>;
  presence: Store<GamePresenceState>;
}

/**
 * 游戏在线状态 api (app_gateway WS 5201, same edge as the device plane):
 * reads/writes the per-account switch that decides whether friends can see
 * "in game X" and whether friend DMs are relayed into the game plane. The
 * gateway pins player_id to the logged-in user — the client always asks
 * about itself with an empty player_id. Degradeable like the device plane.
 */
export class GamePresenceApi {
  private readonly conn: ChatConnection;
  private readonly auth: Store<AuthState>;
  private readonly presence: Store<GamePresenceState>;

  constructor(deps: GamePresenceApiDeps) {
    this.conn = deps.conn;
    this.auth = deps.auth;
    this.presence = deps.presence;
  }

  /** Pulls switch + published games; marks the plane down when it fails. */
  async refresh(): Promise<boolean> {
    if (!this.auth.get().loggedIn) return false;
    try {
      const resp = await this.conn.request(GET_GAME_PRESENCE, { playerId: '' });
      if (resp.code !== 0) {
        setGamePresenceUnavailable(this.presence, true);
        return false;
      }
      applyGamePresence(this.presence, {
        enabled: resp.enabled,
        games: (resp.entries ?? []).map((e: GamePresenceEntry) => e.gameId),
      });
      return true;
    } catch {
      setGamePresenceUnavailable(this.presence, true);
      return false;
    }
  }

  /**
   * Writes the switch. On success the server immediately recomputes the
   * roster (off → DEL + event), so we mirror locally then refresh to pick
   * up the authoritative game list.
   */
  async setEnabled(enabled: boolean): Promise<boolean> {
    if (!this.auth.get().loggedIn) return false;
    const resp = await this.conn.request(SET_GAME_PRESENCE_ENABLED, { playerId: '', enabled });
    if (resp.code !== 0) return false;
    setGamePresenceEnabled(this.presence, enabled);
    await this.refresh();
    return true;
  }

  /** Login success: the mirror starts from the server again. */
  async onLoggedIn(): Promise<void> {
    setGamePresenceUnavailable(this.presence, false);
    await this.refresh();
  }

  logout(): void {
    setGamePresenceUnavailable(this.presence, true);
  }
}
