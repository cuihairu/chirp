import 'store.dart';

/// 游戏在线状态（P0）：per-account mirror of the game-presence switch.
/// Binding a game opts the account in by default — the server reads an unset
/// switch as enabled, so `enabled` starts true and only an explicit choice
/// flips it. `games` lists the bound game ids whose presence is currently
/// published (empty while the switch is off). Like the device plane, the row
/// hides while the edge is down; chat is unaffected either way.
class GamePresenceState {
  const GamePresenceState({
    required this.enabled,
    required this.loaded,
    required this.games,
    required this.unavailable,
  });

  /// 游戏在线状态开关；缺省语义为 true（绑定即默认开启）。
  final bool enabled;

  /// GET_GAME_PRESENCE answered at least once: the switch may be shown.
  final bool loaded;

  /// Bound games with published presence (server-sorted game ids).
  final List<String> games;

  /// Presence plane unreachable: row hides, chat keeps working.
  final bool unavailable;
}

Store<GamePresenceState> createGamePresenceStore() =>
    Store<GamePresenceState>(const GamePresenceState(
      enabled: true,
      loaded: false,
      games: [],
      unavailable: false,
    ));

/// Stores the merged answer of GET_GAME_PRESENCE (server is authoritative).
void applyGamePresence(
  Store<GamePresenceState> store, {
  required bool enabled,
  required List<String> games,
}) {
  store.update((prev) => GamePresenceState(
        enabled: enabled,
        loaded: true,
        games: games,
        unavailable: false,
      ));
}

/// Local write-through of SET_GAME_PRESENCE_ENABLED before the refresh.
/// Turning the switch off also drops the published games locally; the
/// following refresh confirms against the server.
void setGamePresenceEnabled(Store<GamePresenceState> store, bool enabled) {
  store.update((prev) => prev.enabled == enabled
      ? prev
      : GamePresenceState(
          enabled: enabled,
          loaded: prev.loaded,
          games: enabled ? prev.games : const [],
          unavailable: prev.unavailable,
        ));
}

/// Edge down / logged out: hide the row, forget the mirror.
void setGamePresenceUnavailable(
  Store<GamePresenceState> store,
  bool unavailable,
) {
  store.update((prev) {
    if (prev.unavailable == unavailable) return prev;
    return GamePresenceState(
      enabled: prev.enabled,
      loaded: unavailable ? false : prev.loaded,
      games: unavailable ? const [] : prev.games,
      unavailable: unavailable,
    );
  });
}
