import 'store.dart';

class AuthState {
  const AuthState({
    required this.userId,
    required this.kicked,
    required this.deviceId,
    required this.loggedIn,
  });

  /// Logged-in user id; null on the login screen.
  final String? userId;

  /// Server sent KICK_NOTIFY: session taken over by another device.
  final bool kicked;

  /// device_id persisted per install so reconnects never self-kick.
  final String deviceId;

  /// Set once a LOGIN round-trip has succeeded.
  final bool loggedIn;

  AuthState copyWith({
    String? userId,
    bool? kicked,
    bool? loggedIn,
  }) =>
      AuthState(
        userId: userId ?? this.userId,
        kicked: kicked ?? this.kicked,
        deviceId: deviceId,
        loggedIn: loggedIn ?? this.loggedIn,
      );
}

/// The device id source is injected: production wires SharedPreferences so a
/// reconnect of the same install never self-kicks; tests pass a fixed value.
AuthStore createAuthStore(String Function() ensureDeviceId) => AuthStore(
      AuthState(
        userId: null,
        kicked: false,
        deviceId: ensureDeviceId(),
        loggedIn: false,
      ),
    );

class AuthStore extends Store<AuthState> {
  AuthStore(super.initial);
}
