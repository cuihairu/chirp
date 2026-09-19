import 'package:flutter_local_notifications/flutter_local_notifications.dart';

/// Thin wrapper over flutter_local_notifications. Everything degrades: when
/// init or show fails the app keeps working with in-app delivery only, and
/// real background push awaits the backend push transport anyway.
class LocalNotifications {
  LocalNotifications({FlutterLocalNotificationsPlugin? plugin})
      : _plugin = plugin ?? FlutterLocalNotificationsPlugin();

  final FlutterLocalNotificationsPlugin _plugin;
  bool _ready = false;

  Future<void> init() async {
    if (_ready) return;
    try {
      const android = AndroidInitializationSettings('@mipmap/ic_launcher');
      await _plugin.initialize(
        const InitializationSettings(android: android),
      );
      _ready = true;
    } catch (_) {
      _ready = false;
    }
  }

  /// Android 13+ runtime gate; safe no-op on older versions. Returns the
  /// permission state best-effort.
  Future<bool> requestPermission() async {
    try {
      final android = _plugin.resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin>();
      if (android == null) return false;
      return await android.requestNotificationsPermission() ?? false;
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
        ),
      );
    } catch (_) {
      // In-app delivery keeps working.
    }
  }
}
