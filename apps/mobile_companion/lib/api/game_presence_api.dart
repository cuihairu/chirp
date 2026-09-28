import 'package:chirp_proto/chirp_proto.dart';

import '../protocol/chat_connection.dart';
import '../protocol/msg_map.dart' as specs;
import '../state/auth_store.dart';
import '../state/game_presence_store.dart';
import '../state/store.dart';

/// 游戏在线状态 api (app_gateway WS 5201, same edge as the device plane):
/// reads/writes the per-account switch that decides whether friends can see
/// "in game X" and whether friend DMs are relayed into the game plane. The
/// gateway pins player_id to the logged-in user — the client always asks
/// about itself with an empty player_id. Degradeable like the device plane.
class GamePresenceApi {
  GamePresenceApi({
    required this.conn,
    required this.auth,
    required this.presence,
  });

  final ChatConnection conn;
  final AuthStore auth;
  final Store<GamePresenceState> presence;

  /// Pulls switch + published games; marks the plane down when it fails.
  Future<bool> refresh() async {
    if (!auth.value.loggedIn) return false;
    try {
      final resp = await conn.request(
          specs.getGamePresence, GetGamePresenceRequest(playerId: ''));
      if (resp.code != ErrorCode.OK) {
        setGamePresenceUnavailable(presence, true);
        return false;
      }
      applyGamePresence(
        presence,
        enabled: resp.enabled,
        games: resp.entries.map((e) => e.gameId).toList(),
      );
      return true;
    } catch (_) {
      setGamePresenceUnavailable(presence, true);
      return false;
    }
  }

  /// Writes the switch. On success the server immediately recomputes the
  /// roster (off → DEL + event), so we mirror locally then refresh to pick
  /// up the authoritative game list.
  Future<bool> setEnabled(bool enabled) async {
    if (!auth.value.loggedIn) return false;
    final resp = await conn.request(specs.setGamePresenceEnabled,
        SetGamePresenceEnabledRequest(playerId: '', enabled: enabled));
    if (resp.code != ErrorCode.OK) return false;
    setGamePresenceEnabled(presence, enabled);
    await refresh();
    return true;
  }

  /// Login success: the mirror starts from the server again.
  Future<bool> onLoggedIn() async {
    setGamePresenceUnavailable(presence, false);
    return refresh();
  }

  void logout() {
    setGamePresenceUnavailable(presence, true);
  }
}
