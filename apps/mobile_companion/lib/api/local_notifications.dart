import 'package:flutter_local_notifications/flutter_local_notifications.dart';

/// Thin wrapper over flutter_local_notifications. Everything degrades: when
/// init or show fails the app keeps working with in-app delivery only, and
/// real background push awaits the backend push transport anyway. Windows is
/// not covered by the plugin — initialize throws, the app falls back to
/// in-app delivery.
class LocalNotifications {
  LocalNotifications({FlutterLocalNotificationsPlugin? plugin})
      : _plugin = plugin ?? FlutterLocalNotificationsPlugin();

  final FlutterLocalNotificationsPlugin _plugin;
  bool _ready = false;

  Future<void> init() async {
    if (_ready) return;
    try {
      const android = AndroidInitializationSettings('@mipmap/ic_launcher');
      // macOS asks for its permissions up front (mobile asks on demand via
      // requestPermission below); iOS keeps the on-demand flow.
      const darwin = DarwinInitializationSettings(
        requestAlertPermission: true,
        requestBadgePermission: true,
        requestSoundPermission: true,
      );
      const linux = LinuxInitializationSettings(defaultActionName: '打开');
      await _plugin.initialize(
        const InitializationSettings(
          android: android,
          iOS: darwin,
          macOS: darwin,
          linux: linux,
        ),
      );
      _ready = true;
    } catch (_) {
      _ready = false;
    }
  }

  /// Android 13+ runtime gate (safe no-op on older versions) and the macOS
  /// on-demand grant. Linux needs no permission; platforms without plugin
  /// support report false. Returns the permission state best-effort.
  Future<bool> requestPermission() async {
    try {
      final android = _plugin.resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin>();
      if (android != null) {
        return await android.requestNotificationsPermission() ?? false;
      }
      final mac = _plugin.resolvePlatformSpecificImplementation<
          MacOSFlutterLocalNotificationsPlugin>();
      if (mac != null) {
        final granted = await mac.requestPermissions(
          alert: true,
          badge: true,
          sound: true,
        );
        return granted ?? false;
      }
      final ios = _plugin.resolvePlatformSpecificImplementation<
          IOSFlutterLocalNotificationsPlugin>();
      if (ios != null) {
        return await ios.requestPermissions(
              alert: true,
              badge: true,
              sound: true,
            ) ??
            false;
      }
      return false;
    } catch (_) {
      return false;
    }
  }

  Future<void> show({
    required String title,
    required String body,
    required String channelKey,
    required int id,
  }) async {
    if (!_ready) return;
    try {
      await _plugin.show(
        id,
        title,
        body,
        const NotificationDetails(
          android: AndroidNotificationDetails(
            'chirp_messages',
            '聊天消息',
            channelDescription: '私聊与群聊的实时消息',
            importance: Importance.defaultImportance,
            priority: Priority.defaultPriority,
            tag: 'chirp',
          ),
          macOS: DarwinNotificationDetails(),
          linux: LinuxNotificationDetails(),
        ),
      );
    } catch (_) {
      // In-app delivery keeps working.
    }
  }
}
