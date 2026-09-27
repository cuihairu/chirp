import 'package:chirp_proto/proto/auth.pb.dart' as auth;

import 'store.dart';

/// 多端在线（P0）：本账号其他在线端的清单，按 platform 一个槽位。
/// 由登录响应的 onlineDevices 初始清单（服务端已排除自己）与
/// DEVICES_PRESENCE_NOTIFY 变更事件共同维护。web 伴侣版同名 store 的镜像。
class OnlineDeviceView {
  const OnlineDeviceView({
    required this.platform,
    required this.deviceId,
    required this.online,
    required this.at,
  });

  final String platform;
  final String deviceId;
  final bool online;
  final int at;
}

class OnlineDevicesState {
  const OnlineDevicesState({this.byPlatform = const {}});

  final Map<String, OnlineDeviceView> byPlatform;
}

Store<OnlineDevicesState> createOnlineDevicesStore() =>
    Store(const OnlineDevicesState());

void applyDevicePresence(
  Store<OnlineDevicesState> store,
  auth.DevicePresence entry,
  int now,
) {
  final platform = entry.platform.isEmpty ? 'default' : entry.platform;
  final next = Map.of(store.value.byPlatform);
  next[platform] = OnlineDeviceView(
    platform: platform,
    deviceId: entry.deviceId,
    online: entry.online,
    at: now,
  );
  store.value = OnlineDevicesState(byPlatform: next);
}

void applyPresenceList(
  Store<OnlineDevicesState> store,
  Iterable<auth.DevicePresence> devices,
  int now,
) {
  for (final entry in devices) {
    applyDevicePresence(store, entry, now);
  }
}

void resetOnlineDevices(Store<OnlineDevicesState> store) {
  store.value = const OnlineDevicesState();
}

List<OnlineDeviceView> onlineDevicesOf(OnlineDevicesState state) {
  final list = state.byPlatform.values.toList()
    ..sort((a, b) => a.platform.compareTo(b.platform));
  return list;
}
