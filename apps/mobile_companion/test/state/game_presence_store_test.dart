import 'package:chirp_mobile/state/game_presence_store.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('starts at the default-enabled, not-yet-loaded shape', () {
    final store = createGamePresenceStore();
    // 缺省语义:从未设置过的账号 = 开启(绑定即默认开启)。
    expect(store.value.enabled, isTrue);
    expect(store.value.loaded, isFalse);
    expect(store.value.games, isEmpty);
  });

  test('applyGamePresence stores the authoritative snapshot', () {
    final store = createGamePresenceStore();
    applyGamePresence(store, enabled: false, games: const []);
    expect(store.value.enabled, isFalse);
    expect(store.value.loaded, isTrue);
    expect(store.value.unavailable, isFalse);
    applyGamePresence(store, enabled: true, games: const ['game_a', 'game_b']);
    expect(store.value.games, ['game_a', 'game_b']);
  });

  test('switching off drops the published games locally', () {
    final store = createGamePresenceStore();
    applyGamePresence(store, enabled: true, games: const ['game_a']);
    setGamePresenceEnabled(store, false);
    expect(store.value.enabled, isFalse);
    expect(store.value.games, isEmpty);
    // 幂等:同值写入不换快照。
    final before = store.value;
    setGamePresenceEnabled(store, false);
    expect(identical(store.value, before), isTrue);
  });

  test('going unavailable forgets the mirror; refresh brings it back', () {
    final store = createGamePresenceStore();
    applyGamePresence(store, enabled: true, games: const ['game_a']);
    setGamePresenceUnavailable(store, true);
    expect(store.value.unavailable, isTrue);
    expect(store.value.loaded, isFalse);
    expect(store.value.games, isEmpty);
    applyGamePresence(store, enabled: true, games: const ['game_a']);
    expect(store.value.unavailable, isFalse);
  });
}
