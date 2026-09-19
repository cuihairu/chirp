import 'dart:math';

import 'package:shared_preferences/shared_preferences.dart';

const _deviceIdKey = 'chirp.device_id';

/// Random per-install device id, persisted like the web companion's
/// localStorage entry. The server kicks the previous session of the same
/// (user, device) pair, so a stable id means an ordinary reconnect never
/// logs the user out; a fresh install may take the session over.
Future<String> ensureDeviceId(SharedPreferences prefs) async {
  final existing = prefs.getString(_deviceIdKey);
  if (existing != null) return existing;
  final random = Random.secure();
  const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
  final fresh = 'mob-${List.generate(
    8,
    (_) => chars[random.nextInt(chars.length)],
  ).join()}';
  await prefs.setString(_deviceIdKey, fresh);
  return fresh;
}
