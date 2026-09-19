import 'package:chirp_proto/chirp_proto.dart';

import '../protocol/chat_connection.dart';
import '../protocol/chirp_client.dart';
import '../protocol/msg_map.dart' as specs;
import '../state/auth_store.dart';
import '../state/device_store.dart';
import '../state/store.dart';

/// Device plane api (app_gateway WS 5201): registers this install as a push
/// target and lists/unregisters the account's devices. Degradeable like the
/// social and party planes — when the edge is down chat keeps working and the
/// devices entry hides. The server pins user_id on every forwarded request,
/// so nothing here can address another account.
class DeviceApi {
  DeviceApi({
    required this.conn,
    required this.auth,
    required this.devices,
    required this.deviceSummary,
  });

  final ChatConnection conn;
  final AuthStore auth;
  final Store<DeviceState> devices;

  /// Human-readable device name ("Android · Pixel 7"); the caller derives it
  /// from the platform device info, mirroring the web's ua_summary.
  final String deviceSummary;

  Future<bool> login(String userId) async {
    if (conn.status != ConnStatus.connected) {
      await conn.connect();
    }
    final resp = await conn.request(
        specs.login,
        LoginRequest(
          token: userId,
          deviceId: auth.value.deviceId,
          platform: 'android',
        ));
    if (resp.code != ErrorCode.OK) {
      conn.disconnect();
      setUnavailable(devices, true);
      return false;
    }
    conn.resetBackoff();
    setUnavailable(devices, false);
    await registerSelf();
    return true;
  }

  void logout() {
    conn.disconnect();
    setUnavailable(devices, true);
  }

  /// Registers this install as a push target, then refreshes the list.
  Future<void> registerSelf() async {
    final userId = auth.value.userId;
    final deviceId = auth.value.deviceId;
    if (userId == null || deviceId.isEmpty) return;
    // No FCM/APNs token wired yet: pushes to this device degrade to the
    // provider's logging transport until the real push transport lands
    // (TODO: 真实推送传输).
    final resp = await conn.request(
        specs.registerDevice,
        RegisterDeviceRequest(
          userId: userId,
          deviceId: deviceId,
          platform: 'android',
          deviceName: deviceSummary,
        ));
    setSelfRegistered(devices, resp.code == ErrorCode.OK);
    await refreshDevices();
  }

  Future<void> refreshDevices() async {
    final userId = auth.value.userId;
    if (userId == null) return;
    final resp = await conn.request(
        specs.getUserDevices, GetUserDevicesRequest(userId: userId));
    if (resp.code != ErrorCode.OK) return;
    setDevices(
      devices,
      resp.devices
          .map((d) => DeviceEntry(
                deviceId: d.deviceId,
                platform: d.platform,
                deviceName: d.deviceName,
                appVersion: d.appVersion,
                osVersion: d.osVersion,
                registeredAt: d.registeredAt.toInt(),
                isActive: d.isActive,
              ))
          .toList(),
    );
  }

  /// Removes a push target; removing our own only drops the badge state.
  Future<bool> unregister(String deviceId) async {
    final userId = auth.value.userId;
    if (userId == null) return false;
    final resp = await conn.request(
        specs.unregisterDevice,
        UnregisterDeviceRequest(
          userId: userId,
          deviceId: deviceId,
        ));
    if (resp.code != ErrorCode.OK) return false;
    if (deviceId == auth.value.deviceId) {
      setSelfRegistered(devices, false);
    }
    await refreshDevices();
    return true;
  }

  bool isSelf(String deviceId) => deviceId == auth.value.deviceId;
}
