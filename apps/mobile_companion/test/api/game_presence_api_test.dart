import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_mobile/api/game_presence_api.dart';
import 'package:chirp_mobile/state/auth_store.dart';
import 'package:chirp_mobile/state/game_presence_store.dart';
import 'package:chirp_mobile/state/store.dart';
import 'package:flutter_test/flutter_test.dart';

import 'chat_api_test.dart';

GamePresenceApi makeApi(FakeConnection conn, Store<GamePresenceState> presence,
    {bool loggedIn = true}) {
  final auth = createAuthStore(() => 'device-1');
  if (loggedIn) {
    auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
  }
  return GamePresenceApi(conn: conn, auth: auth, presence: presence);
}

void main() {
  group('GamePresenceApi.refresh', () {
    test('pulls switch + games and marks the store loaded', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      conn.responses[MsgID.GET_GAME_PRESENCE_REQ] = (req) {
        // player_id 由 edge 钉死,客户端只发空串。
        expect(req, isA<GetGamePresenceRequest>());
        expect((req as GetGamePresenceRequest).playerId, isEmpty);
        return GetGamePresenceResponse(
          code: ErrorCode.OK,
          enabled: true,
          entries: [GamePresenceEntry(gameId: 'game_a', gameUserId: 'ga1')],
        );
      };
      final api = makeApi(conn, presence);
      expect(await api.refresh(), isTrue);
      expect(presence.value.enabled, isTrue);
      expect(presence.value.loaded, isTrue);
      expect(presence.value.games, ['game_a']);
    });

    test('a disabled account comes back with no games', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      conn.responses[MsgID.GET_GAME_PRESENCE_REQ] =
          (_) => GetGamePresenceResponse(code: ErrorCode.OK, enabled: false);
      final api = makeApi(conn, presence);
      await api.refresh();
      expect(presence.value.enabled, isFalse);
      expect(presence.value.games, isEmpty);
    });

    test('marks the plane unavailable when the edge errors', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      // 未脚本化的请求会抛错 —— 与真实断连同路径。
      final api = makeApi(conn, presence);
      expect(await api.refresh(), isFalse);
      expect(presence.value.unavailable, isTrue);
      expect(presence.value.loaded, isFalse);
    });

    test('a rejected read also marks unavailable', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      conn.responses[MsgID.GET_GAME_PRESENCE_REQ] =
          (_) => GetGamePresenceResponse(code: ErrorCode.AUTH_FAILED);
      final api = makeApi(conn, presence);
      expect(await api.refresh(), isFalse);
      expect(presence.value.unavailable, isTrue);
    });

    test('stays silent while logged out', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      final api = makeApi(conn, presence, loggedIn: false);
      expect(await api.refresh(), isFalse);
      expect(conn.requests, isEmpty);
    });
  });

  group('GamePresenceApi.setEnabled', () {
    test('writes the switch then refreshes to the cleared list', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      var enabled = true;
      conn.responses[MsgID.SET_GAME_PRESENCE_ENABLED_REQ] = (req) {
        final r = req as SetGamePresenceEnabledRequest;
        expect(r.playerId, isEmpty); // 客户端永远只写自己
        expect(r.enabled, isFalse);
        enabled = false;
        return SetGamePresenceEnabledResponse(code: ErrorCode.OK);
      };
      conn.responses[MsgID.GET_GAME_PRESENCE_REQ] = (_) => enabled
          ? GetGamePresenceResponse(
              code: ErrorCode.OK,
              enabled: true,
              entries: [GamePresenceEntry(gameId: 'game_a')])
          : GetGamePresenceResponse(code: ErrorCode.OK, enabled: false);
      final api = makeApi(conn, presence);
      await api.refresh(); // prime: enabled with one game
      expect(presence.value.games, ['game_a']);

      expect(await api.setEnabled(false), isTrue);
      // 写成功后立即刷新:关闭会让服务端清掉 roster,游戏清单必须跟着清空。
      expect(conn.requests.map((r) => r.$1).toList(), [
        MsgID.GET_GAME_PRESENCE_REQ,
        MsgID.SET_GAME_PRESENCE_ENABLED_REQ,
        MsgID.GET_GAME_PRESENCE_REQ,
      ]);
      expect(presence.value.enabled, isFalse);
      expect(presence.value.games, isEmpty);
    });

    test('a rejected write leaves the mirror untouched', () async {
      final conn = FakeConnection();
      final presence = createGamePresenceStore();
      conn.responses[MsgID.GET_GAME_PRESENCE_REQ] =
          (_) => GetGamePresenceResponse(code: ErrorCode.OK, enabled: true);
      conn.responses[MsgID.SET_GAME_PRESENCE_ENABLED_REQ] =
          (_) => SetGamePresenceEnabledResponse(code: ErrorCode.INVALID_PARAM);
      final api = makeApi(conn, presence);
      await api.refresh();
      conn.requests.clear();

      expect(await api.setEnabled(false), isFalse);
      expect(conn.requests, hasLength(1)); // 写失败不再刷新
      expect(presence.value.enabled, isTrue);
    });
  });

  test('logout hides the section; onLoggedIn re-pulls the mirror', () async {
    final conn = FakeConnection();
    final presence = createGamePresenceStore();
    conn.responses[MsgID.GET_GAME_PRESENCE_REQ] =
        (_) => GetGamePresenceResponse(code: ErrorCode.OK, enabled: false);
    final api = makeApi(conn, presence);
    await api.onLoggedIn();
    expect(presence.value.loaded, isTrue);
    api.logout();
    expect(presence.value.unavailable, isTrue);
  });
}
