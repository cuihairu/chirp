import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_mobile/api/local_notifications.dart';
import 'package:chirp_mobile/api/services.dart';
import 'package:chirp_mobile/ui/app_root.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../api/chat_api_test.dart';

/// Chat + device plane on scripted fakes; everything else absent (the
/// presence RPCs ride the app_gateway socket, so deviceConn is what makes
/// the section appear).
class Planes {
  Planes({bool enabled = true}) {
    _live = enabled;
    chat.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.OK, userId: 'a');
    chat.responses[MsgID.GET_USER_GROUPS_REQ] =
        (_) => GetUserGroupsResponse(code: ErrorCode.OK);
    device.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.OK, userId: 'a');
    device.responses[MsgID.REGISTER_DEVICE_REQ] =
        (_) => RegisterDeviceResponse(code: ErrorCode.OK);
    device.responses[MsgID.GET_USER_DEVICES_REQ] =
        (_) => GetUserDevicesResponse(code: ErrorCode.OK);
    device.responses[MsgID.GET_GAME_PRESENCE_REQ] = (_) => _live
        ? GetGamePresenceResponse(
            code: ErrorCode.OK,
            enabled: true,
            entries: [GamePresenceEntry(gameId: 'game_a')])
        : GetGamePresenceResponse(code: ErrorCode.OK, enabled: false);
    device.responses[MsgID.SET_GAME_PRESENCE_ENABLED_REQ] = (req) {
      _live = (req as SetGamePresenceEnabledRequest).enabled;
      return SetGamePresenceEnabledResponse(code: ErrorCode.OK);
    };
  }

  final chat = FakeConnection();
  final device = FakeConnection();
  late bool _live;

  Services services() => createServices(
        conn: chat,
        deviceConn: device,
        ensureDeviceId: () => 'dev-1',
      );
}

Future<Services> pumpLoggedIn(WidgetTester tester, Planes planes) async {
  final services = planes.services();
  await tester.pumpWidget(Directionality(
    textDirection: TextDirection.ltr,
    child: ChirpApp(services: services, notifications: LocalNotifications()),
  ));
  await tester.pump();
  await tester.enterText(find.byType(TextField), 'alice');
  await tester.tap(find.text('登录'));
  await tester.pumpAndSettle();
  await tester.tap(find.text('我的'));
  await tester.pumpAndSettle();
  return services;
}

SwitchListTile switchTile(WidgetTester tester) => tester.widget<SwitchListTile>(
    find.byKey(const Key('game-presence-switch')));

void main() {
  testWidgets('我的 tab renders the switch with the bound game', (tester) async {
    await pumpLoggedIn(tester, Planes());
    final tile = switchTile(tester);
    expect(tile.value, isTrue);
    expect(tile.onChanged, isNotNull); // 拉到权威快照后可点
    expect(find.text('当前生效:game_a'), findsOneWidget);
  });

  testWidgets('a never-set account still shows enabled (默认开启语义)',
      (tester) async {
    final planes = Planes();
    // GET answers enabled with zero entries — the account bound nothing yet.
    planes.device.responses[MsgID.GET_GAME_PRESENCE_REQ] = (_) =>
        GetGamePresenceResponse(code: ErrorCode.OK, enabled: true);
    await pumpLoggedIn(tester, planes);
    expect(switchTile(tester).value, isTrue);
    expect(find.text('尚未绑定游戏,绑定后自动开始上报在线状态'), findsOneWidget);
  });

  testWidgets('toggling off writes the switch and updates the copy',
      (tester) async {
    final planes = Planes();
    await pumpLoggedIn(tester, planes);
    await tester.tap(find.byKey(const Key('game-presence-switch')));
    await tester.pumpAndSettle();
    expect(switchTile(tester).value, isFalse);
    expect(find.text('已关闭:好友看不到你的游戏状态,消息也不进游戏'), findsOneWidget);
    expect(find.text('已关闭游戏在线状态'), findsOneWidget); // snack
    // 写请求确实落到 edge,player_id 永远空串(由服务端钉死)。
    final write = planes.device.requests
        .firstWhere((r) => r.$1 == MsgID.SET_GAME_PRESENCE_ENABLED_REQ)
        .$2 as SetGamePresenceEnabledRequest;
    expect(write.playerId, isEmpty);
    expect(write.enabled, isFalse);
  });

  testWidgets('a closed account starts unchecked', (tester) async {
    final planes = Planes(enabled: false);
    planes._live = false;
    await pumpLoggedIn(tester, planes);
    expect(switchTile(tester).value, isFalse);
    expect(find.text('已关闭:好友看不到你的游戏状态,消息也不进游戏'), findsOneWidget);
  });

  testWidgets('the section hides without the app_gateway plane',
      (tester) async {
    final chat = FakeConnection();
    chat.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.OK, userId: 'a');
    chat.responses[MsgID.GET_USER_GROUPS_REQ] =
        (_) => GetUserGroupsResponse(code: ErrorCode.OK);
    final services = createServices(conn: chat, ensureDeviceId: () => 'dev-1');
    await tester.pumpWidget(Directionality(
      textDirection: TextDirection.ltr,
      child: ChirpApp(services: services, notifications: LocalNotifications()),
    ));
    await tester.pump();
    await tester.enterText(find.byType(TextField), 'alice');
    await tester.tap(find.text('登录'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('我的'));
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('game-presence-switch')), findsNothing);
  });
}
