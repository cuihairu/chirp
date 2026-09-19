import 'store.dart';

/// Narrowed copy of chirp.notification.DeviceInfo the UI renders.
class DeviceEntry {
  const DeviceEntry({
    required this.deviceId,
    required this.platform,
    required this.deviceName,
    required this.appVersion,
    required this.osVersion,
    required this.registeredAt,
    required this.isActive,
  });

  final String deviceId;
  final String platform;
  final String deviceName;
  final String appVersion;
  final String osVersion;
  final int registeredAt;
  final bool isActive;
}

/// Mirror of the device plane (app_gateway → notification): which push
/// targets our account has and whether this install is one of them. The
/// device store is memory-only server-side, so the list is rebuilt on every
/// login; `unavailable` hides the entry while chat keeps working.
class DeviceState {
  const DeviceState({
    required this.devices,
    required this.selfRegistered,
    required this.unavailable,
  });

  final List<DeviceEntry> devices;

  /// This install completed REGISTER_DEVICE (best-effort).
  final bool selfRegistered;

  /// Device plane unreachable / not logged in: entry hides, chat unaffected.
  final bool unavailable;
}

Store<DeviceState> createDeviceStore() => Store<DeviceState>(const DeviceState(
      devices: [],
      selfRegistered: false,
      unavailable: false,
    ));

/// Replaces the list from GET_USER_DEVICES (server is authoritative).
void setDevices(Store<DeviceState> store, List<DeviceEntry> devices) {
  store.update((prev) => DeviceState(
        devices: devices,
        selfRegistered: prev.selfRegistered,
        unavailable: prev.unavailable,
      ));
}

void setSelfRegistered(Store<DeviceState> store, bool registered) {
  store.update((prev) => prev.selfRegistered == registered
      ? prev
      : DeviceState(
          devices: prev.devices,
          selfRegistered: registered,
          unavailable: prev.unavailable,
        ));
}

/// Login failure, kick, or a dead edge: hide the feature, keep chat.
void setUnavailable(Store<DeviceState> store, bool unavailable) {
  store.update((prev) {
    if (prev.unavailable == unavailable) return prev;
    // Coming back online starts from a clean list; going down forgets it.
    return DeviceState(
      devices: unavailable ? const [] : prev.devices,
      selfRegistered: unavailable ? false : prev.selfRegistered,
      unavailable: unavailable,
    );
  });
}
