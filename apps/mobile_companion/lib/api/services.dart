import 'package:flutter/foundation.dart';

import '../protocol/chirp_client.dart';
import '../protocol/chat_connection.dart';
import '../state/auth_store.dart';
import '../state/conversation_store.dart';
import '../state/device_store.dart';
import '../state/friend_store.dart';
import '../state/message_store.dart';
import '../state/party_store.dart';
import '../state/store.dart';
import '../state/typing_presence.dart';
import 'chat_api.dart';
import 'device_api.dart';
import 'local_notifications.dart';
import 'party_api.dart';
import 'social_api.dart';

/// One object graph per app process: the chat websocket + stores + ChatApi,
/// plus the optional social / party / device planes (second/third/fourth
/// websockets) that are degradeable — when any is absent or down, chat works
/// and its features hide. Tests inject fake connections via
/// `createServices(conn: ..., socialConn: ...)`.
class Services {
  Services({
    required this.client,
    required this.api,
    required this.auth,
    required this.conversations,
    required this.messages,
    required this.typing,
    required this.presence,
    required this.friends,
    required this.partyState,
    required this.devices,
    this.social,
    this.socialApi,
    this.party,
    this.partyApi,
    this.device,
    this.deviceApi,
    this.notifications,
  });

  final ChatConnection client;
  final ChatApi api;

  /// Social-plane connection; null when social is not configured.
  final ChatConnection? social;

  /// null when social is not configured; goes inert when social is down.
  final SocialApi? socialApi;

  /// Party-plane connection; null when party is not configured.
  final ChatConnection? party;

  /// null when party is not configured; goes inert when party is down.
  final PartyApi? partyApi;

  /// Device-plane connection (app_gateway); null when not configured.
  final ChatConnection? device;

  /// null when the device plane is not configured; inert when it is down.
  final DeviceApi? deviceApi;

  /// Local notification surface (shared with the root widget); null in tests.
  final LocalNotifications? notifications;
  final AuthStore auth;
  final Store<ConversationState> conversations;
  final Store<MessageState> messages;
  final Store<TypingState> typing;
  final Store<PresenceState> presence;
  final Store<FriendState> friends;
  final Store<PartyState> partyState;
  final Store<DeviceState> devices;

  /// Chat login, then best-effort logins on the degradeable planes.
  Future<void> loginAll(String userId) async {
    await api.login(userId);
    await socialApi?.login(userId).catchError((Object _) => false);
    await partyApi?.login(userId).catchError((Object _) => false);
    await deviceApi?.login(userId).catchError((Object _) => false);
  }

  /// Everything signs out; chat last (it owns the auth store reset).
  Future<void> logoutAll() async {
    deviceApi?.logout();
    socialApi?.logout();
    partyApi?.logout();
    await api.logout();
  }
}

/// Dev defaults. The Android emulator reaches the host machine via 10.0.2.2;
/// iOS simulators and the desktop builds run on the host itself, so they use
/// the loopback. Override per build with
/// --dart-define=CHIRP_CHAT_WS_URL=ws://... (same for the other planes).
const _chatUrlOverride = String.fromEnvironment('CHIRP_CHAT_WS_URL');
const _socialUrlOverride = String.fromEnvironment('CHIRP_SOCIAL_WS_URL');
const _partyUrlOverride = String.fromEnvironment('CHIRP_PARTY_WS_URL');
const _deviceUrlOverride = String.fromEnvironment('CHIRP_DEVICE_WS_URL');

// String.fromEnvironment needs a const defaultValue, so the platform pick
// happens here instead: emulator host alias vs plain loopback.
String get _defaultHost =>
    defaultTargetPlatform == TargetPlatform.android ? '10.0.2.2' : '127.0.0.1';

String get defaultChatUrl =>
    _chatUrlOverride.isNotEmpty ? _chatUrlOverride : 'ws://$_defaultHost:7001';
String get defaultSocialUrl => _socialUrlOverride.isNotEmpty
    ? _socialUrlOverride
    : 'ws://$_defaultHost:8001';
String get defaultPartyUrl => _partyUrlOverride.isNotEmpty
    ? _partyUrlOverride
    : 'ws://$_defaultHost:7501';
String get defaultDeviceUrl => _deviceUrlOverride.isNotEmpty
    ? _deviceUrlOverride
    : 'ws://$_defaultHost:5201';

Services createServices({
  String? url,
  ChatConnection? conn,
  String? socialUrl,
  ChatConnection? socialConn,
  String? partyUrl,
  ChatConnection? partyConn,
  String? deviceUrl,
  ChatConnection? deviceConn,
  required String Function() ensureDeviceId,
  String deviceSummary = '移动端',
  LocalNotifications? notifications,
}) {
  final client = conn ?? ChirpClient(url: url ?? defaultChatUrl);
  final auth = createAuthStore(ensureDeviceId);
  final conversations = createConversationStore();
  final messages = createMessageStore();
  final typing = createTypingStore();
  final presence = createPresenceStore();
  final friends = createFriendStore();
  final partyState = createPartyStore();
  final devices = createDeviceStore();
  final api = ChatApi(
    conn: client,
    auth: auth,
    conversations: conversations,
    messages: messages,
    typing: typing,
  );

  // The social/party/device planes default ON for real ChirpClients; tests
  // that inject a chat fake get chat-only unless they also inject the plane.
  final socialConn2 = socialConn ??
      (conn != null ? null : ChirpClient(url: socialUrl ?? defaultSocialUrl));
  final socialApi = socialConn2 == null
      ? null
      : SocialApi(
          conn: socialConn2, auth: auth, presence: presence, friends: friends);
  final partyConn2 = partyConn ??
      (conn != null ? null : ChirpClient(url: partyUrl ?? defaultPartyUrl));
  final partyApi = partyConn2 == null
      ? null
      : PartyApi(conn: partyConn2, auth: auth, party: partyState);
  final deviceConn2 = deviceConn ??
      (conn != null ? null : ChirpClient(url: deviceUrl ?? defaultDeviceUrl));
  final deviceApi = deviceConn2 == null
      ? null
      : DeviceApi(
          conn: deviceConn2,
          auth: auth,
          devices: devices,
          deviceSummary: deviceSummary,
        );

  return Services(
    client: client,
    api: api,
    social: socialConn2,
    socialApi: socialApi,
    party: partyConn2,
    partyApi: partyApi,
    device: deviceConn2,
    deviceApi: deviceApi,
    notifications: notifications,
    auth: auth,
    conversations: conversations,
    messages: messages,
    typing: typing,
    presence: presence,
    friends: friends,
    partyState: partyState,
    devices: devices,
  );
}
