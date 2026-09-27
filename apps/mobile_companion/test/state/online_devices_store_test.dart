import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_proto/proto/auth.pb.dart' as pbauth;
import 'package:fixnum/fixnum.dart';
import 'package:chirp_mobile/state/online_devices_store.dart';
import 'package:flutter_test/flutter_test.dart';

import '../api/chat_api_test.dart';

void main() {
  test('store keeps one slot per platform; offline marks without dropping',
      () {
    final store = createOnlineDevicesStore();
    applyPresenceList(store, [
      pbauth.DevicePresence(platform: 'ios', deviceId: 'p1', online: true, ts: Int64(1)),
      pbauth.DevicePresence(
          platform: 'android', deviceId: 'p2', online: true, ts: Int64(1)),
    ], 1);
    expect(onlineDevicesOf(store.value).map((d) => d.platform),
        ['android', 'ios']);

    // 同 platform 新事件覆盖旧条目（同型互顶后只剩一条）。
    applyDevicePresence(store,
        pbauth.DevicePresence(platform: 'ios', deviceId: 'p9', online: true),
        2);
    expect(store.value.byPlatform['ios']!.deviceId, 'p9');
    expect(store.value.byPlatform['ios']!.at, 2);
    expect(onlineDevicesOf(store.value), hasLength(2));

    // 下线保留条目只翻标志。
    applyDevicePresence(store,
        pbauth.DevicePresence(platform: 'ios', deviceId: 'p9', online: false),
        3);
    expect(store.value.byPlatform['ios']!.online, isFalse);
    expect(store.value.byPlatform['ios']!.deviceId, 'p9');

    // 空 platform 归一 default;reset 清空。
    applyDevicePresence(
        store, pbauth.DevicePresence(deviceId: 'legacy', online: true), 4);
    expect(store.value.byPlatform['default']!.platform, 'default');
    resetOnlineDevices(store);
    expect(store.value.byPlatform, isEmpty);
  });

  test('login seeds the initial list; notify events update it', () async {
    final h = Harness(onlineDevices: createOnlineDevicesStore());
    h.conn.responses[MsgID.LOGIN_REQ] = (_) => LoginResponse(
          code: ErrorCode.OK,
          userId: 'a',
          onlineDevices: [
            pbauth.DevicePresence(
                platform: 'web', deviceId: 'tab-1', online: true, ts: Int64(7)),
          ],
        );
    await h.api.login('a');
    expect(h.api.onlineDevices!.value.byPlatform['web']!.deviceId, 'tab-1');

    // DEVICES_PRESENCE_NOTIFY → store 更新（ios 上线）。
    h.conn.dispatch(
      MsgID.DEVICES_PRESENCE_NOTIFY,
      pbauth.DevicesPresenceNotify(
        devices: [
          pbauth.DevicePresence(
              platform: 'ios', deviceId: 'p1', online: true, ts: Int64(9)),
        ],
      ),
    );
    expect(h.api.onlineDevices!.value.byPlatform['ios']!.deviceId, 'p1');
    expect(h.api.onlineDevices!.value.byPlatform['ios']!.online, isTrue);

    // logout 清空。
    h.conn.responses[MsgID.LOGOUT_REQ] =
        (_) => LogoutResponse(code: ErrorCode.OK);
    await h.api.logout();
    expect(h.api.onlineDevices!.value.byPlatform, isEmpty);
  });

  test('malformed presence notify body is dropped silently', () async {
    final h = Harness(onlineDevices: createOnlineDevicesStore());
    h.conn.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.OK, userId: 'a');
    await h.api.login('a');
    h.conn.notifyHandlers[MsgID.DEVICES_PRESENCE_NOTIFY]!(
        Uint8List.fromList([0xff, 0xff, 0xff]));
    expect(h.api.onlineDevices!.value.byPlatform, isEmpty);
  });
}
