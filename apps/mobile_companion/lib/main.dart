import 'dart:io';

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'api/device_id.dart';
import 'api/local_notifications.dart';
import 'api/services.dart';
import 'ui/app_root.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final prefs = await SharedPreferences.getInstance();
  final deviceId = await ensureDeviceId(prefs);
  final notifications = LocalNotifications();
  await notifications.init();
  final services = createServices(
    ensureDeviceId: () => deviceId,
    deviceSummary: deviceSummaryOf(prefs),
    notifications: notifications,
  );
  runApp(ChirpApp(services: services, notifications: notifications));
}

/// The registered device display name (mirrors the web's ua_summary):
/// platform plus a stored model string captured at first login.
String deviceSummaryOf(SharedPreferences prefs) {
  final platform = platformName();
  final stored = prefs.getString('chirp.device_model');
  return stored == null ? platform : '$platform · $stored';
}

String platformName() {
  if (Platform.isIOS) return 'iOS';
  if (Platform.isAndroid) return 'Android';
  if (Platform.isMacOS) return 'macOS';
  if (Platform.isWindows) return 'Windows';
  if (Platform.isLinux) return 'Linux';
  return '移动端';
}
