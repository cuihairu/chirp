import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_proto/proto/chat.pb.dart' as chat;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/api/local_notifications.dart';
import 'package:chirp_mobile/api/services.dart';
import 'package:chirp_mobile/protocol/chat_connection.dart';
import 'package:chirp_mobile/protocol/chirp_client.dart';
import 'package:chirp_mobile/protocol/msg_map.dart';
import 'package:chirp_mobile/ui/app_root.dart';
import 'package:protobuf/protobuf.dart';

/// Chat-only connection: canned LOGIN/LOGOUT responses, manual status
/// fan-out. Planes are absent (injected chat conn ⇒ createServices keeps
/// the app chat-only), which is also the degrade path we want to exercise.
class FakeConnection implements ChatConnection {
  GeneratedMessage loginResponse =
      LoginResponse(code: ErrorCode.OK, userId: 'a');
  final listeners = <void Function(ConnStatus)>[];
  int connectCount = 0;

  /// Mirrors a real socket: only an open connection reports `connected`,
  /// and login must actually call connect() to open it.
  bool open = false;

  void triggerStatus(ConnStatus status) {
    for (final listener in List.of(listeners)) {
      listener(status);
    }
  }

  @override
  Future<void> connect() async {
    open = true;
    connectCount++;
  }

  @override
  void disconnect() {
    open = false;
  }

  @override
  void resetBackoff() {}

  @override
  Future<TResp> request<TResp extends GeneratedMessage>(
    MessageSpec<TResp> spec,
    GeneratedMessage request, {
    int? timeoutMs,
  }) async {
    final GeneratedMessage resp;
    if (spec.reqMsgId == MsgID.LOGIN_REQ) {
      resp = loginResponse;
    } else if (spec.reqMsgId == MsgID.LOGOUT_REQ) {
      resp = LogoutResponse(code: ErrorCode.OK);
    } else if (spec.reqMsgId == MsgID.GET_USER_GROUPS_REQ) {
      // HomeScreen refreshes its group list on entry; empty roster is fine.
      resp = chat.GetUserGroupsResponse(code: ErrorCode.OK);
    } else {
      throw StateError('unexpected request ${spec.reqMsgId.name}');
    }
    return spec.decodeResponse(resp.writeToBuffer());
  }

  @override
  void send(MsgID msgId, Uint8List body) {}

  @override
  void Function() onNotify(MsgID msgId, void Function(Uint8List) handler) =>
      () {};

  @override
  @override
  void Function() onStatus(void Function(ConnStatus) listener) {
    listeners.add(listener);
    return () {};
  }

  @override
  void Function() onReconnecting(void Function(int attempt, int delayMs) listener) =>
      () {};

  @override
  void Function() onReconnected(void Function() listener) => () {};

  @override
  void heartbeatNow() {}

  @override
  ConnStatus get status => open ? ConnStatus.connected : ConnStatus.idle;

  @override
  bool get kicked => false;
}

Future<void> pumpApp(WidgetTester tester, Services services) async {
  await tester.pumpWidget(
    Directionality(
      textDirection: TextDirection.ltr,
      child: ChirpApp(
        services: services,
        notifications: LocalNotifications(),
      ),
    ),
  );
  await tester.pump();
}

void main() {
  testWidgets('login navigates to home; the four tabs exist', (tester) async {
    final conn = FakeConnection();
    final services = createServices(conn: conn, ensureDeviceId: () => 'dev-1');
    await pumpApp(tester, services);

    expect(find.text('Chirp'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'alice');
    await tester.tap(find.text('登录'));
    await tester.pumpAndSettle();

    expect(find.text('会话'), findsWidgets); // app bar + nav destination
    expect(services.auth.value.loggedIn, isTrue);
    expect(conn.connectCount, 1);
    // Chat-only services: degrade hints must show on the social tab.
    await tester.tap(find.text('好友'));
    await tester.pumpAndSettle();
    expect(find.text('社交服务未连接,好友功能暂不可用'), findsOneWidget);
    await tester.tap(find.text('我的'));
    await tester.pumpAndSettle();
    expect(find.text('alice'), findsOneWidget);
    expect(find.textContaining('dev-1'), findsOneWidget);
  });

  testWidgets('failed login keeps the user on the login screen with copy',
      (tester) async {
    final conn = FakeConnection();
    conn.loginResponse = LoginResponse(code: ErrorCode.AUTH_FAILED);
    final services = createServices(conn: conn, ensureDeviceId: () => 'dev-1');
    await pumpApp(tester, services);

    await tester.enterText(find.byType(TextField), 'alice');
    await tester.tap(find.text('登录'));
    await tester.pumpAndSettle();

    expect(services.auth.value.loggedIn, isFalse);
    expect(find.text('认证失败'), findsOneWidget);
    expect(find.text('登录'), findsWidgets);
  });

  testWidgets('terminal kick routes back to the login banner', (tester) async {
    final conn = FakeConnection();
    final services = createServices(conn: conn, ensureDeviceId: () => 'dev-1');
    await pumpApp(tester, services);

    await tester.enterText(find.byType(TextField), 'alice');
    await tester.tap(find.text('登录'));
    await tester.pumpAndSettle();
    expect(find.text('好友'), findsOneWidget);

    conn.triggerStatus(ConnStatus.kicked);
    await tester.pumpAndSettle();

    expect(find.text('账号已在其他设备登录,本机会话已下线'), findsOneWidget);
    expect(services.auth.value.kicked, isTrue);
  });

  testWidgets('logout returns to a clean login screen', (tester) async {
    final conn = FakeConnection();
    final services = createServices(conn: conn, ensureDeviceId: () => 'dev-1');
    await pumpApp(tester, services);

    await tester.enterText(find.byType(TextField), 'alice');
    await tester.tap(find.text('登录'));
    await tester.pumpAndSettle();

    await tester.tap(find.text('我的'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('退出登录'));
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, '退出'));
    await tester.pumpAndSettle();

    expect(find.text('Chirp'), findsOneWidget);
    expect(services.auth.value.loggedIn, isFalse);
    expect(services.auth.value.userId, isNull);
  });
}
