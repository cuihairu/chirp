import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';

import '../protocol/chat_connection.dart';
import '../protocol/chirp_client.dart';
import '../protocol/msg_map.dart' as specs;
import '../state/auth_store.dart';
import '../state/friend_store.dart';
import '../state/store.dart';
import '../state/typing_presence.dart';

/// Social plane api (second websocket, port 8001): friends and presence.
/// Degradable by design — when the social socket is unavailable the chat
/// keeps working and the UI simply hides friend features. The backend owns
/// the roster: login pulls friends and pending requests, notifications keep
/// this mirror in sync.
class SocialApi {
  SocialApi({
    required this.conn,
    required this.auth,
    required this.presence,
    required this.friends,
  });

  final ChatConnection conn;
  final AuthStore auth;
  final Store<PresenceState> presence;
  final Store<FriendState> friends;
  final List<void Function()> _unsubs = [];

  Future<bool> login(String userId) async {
    if (conn.status != ConnStatus.connected) {
      await conn.connect();
    }
    // Same LOGIN pair as chat (scaffold token = user id); device_id stays
    // unset and lands on the service's "default" device.
    final resp = await conn.request(
        specs.login, LoginRequest(token: userId, platform: 'android'));
    if (resp.code != ErrorCode.OK) {
      // Degrade quietly: no friend features, chat unaffected.
      conn.disconnect();
      return false;
    }
    conn.resetBackoff();
    // Reset the local mirror (a previous account's roster must not leak),
    // bring notify handlers up first so nothing racing the pull is lost,
    // then load the authoritative roster.
    friends.value =
        const FriendState(friends: [], pendingIn: [], pendingOut: []);
    start();
    await fetchFriends();
    await fetchPending();
    return true;
  }

  void logout() {
    stop();
    conn.disconnect();
  }

  void stop() {
    for (final off in _unsubs) {
      off();
    }
    _unsubs.clear();
  }

  /// Re-pull the authoritative friend list (e.g. after a reconnect).
  Future<int> fetchFriends() async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.getFriendList,
        GetFriendListRequest(
          userId: userId,
          limit: 0,
          offset: 0,
        ));
    if (resp.code != ErrorCode.OK) return resp.code.value;
    replaceFriends(friends, resp.friends.map((f) => f.userId).toList());
    return resp.code.value;
  }

  /// Re-pull the incoming request queue.
  Future<int> fetchPending() async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.getPendingRequests, GetPendingRequestsRequest(userId: userId));
    if (resp.code != ErrorCode.OK) return resp.code.value;
    replacePendingIn(
        friends,
        resp.requests
            .map((r) => PendingRequest(
                requestId: r.requestId, fromUserId: r.fromUserId))
            .toList());
    return resp.code.value;
  }

  /// Advertise our presence; the service broadcasts it to our friends.
  Future<void> publishPresence(PresenceStatus status,
      {String statusMessage = ''}) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    await conn.request(
        specs.setPresence,
        SetPresenceRequest(
          userId: userId,
          status: status,
          statusMessage: statusMessage,
        ));
  }

  /// Bulk pull for the visible roster (conversation peers, friends).
  Future<void> pullPresence(List<String> userIds) async {
    if (userIds.isEmpty) return;
    final resp = await conn.request(
        specs.getPresence,
        GetPresenceRequest(
          userIds: userIds,
        ));
    if (resp.code != ErrorCode.OK) return;
    final at = DateTime.now().millisecondsSinceEpoch;
    for (final entry in resp.presences) {
      setPresence(
          presence, entry.userId, entry.status, entry.statusMessage, at);
    }
  }

  /// Pending-request model: the other side must accept before we are friends.
  Future<int> sendFriendRequest(String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.addFriend,
        AddFriendRequest(
          userId: userId,
          targetUserId: targetUserId,
          message: '',
        ));
    if (resp.code == ErrorCode.OK) addPendingOut(friends, targetUserId);
    return resp.code.value;
  }

  Future<int> respondRequest(String requestId, bool accept) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.friendRequestAction,
        FriendRequestAction(
          userId: userId,
          requestId: requestId,
          accept: accept,
        ));
    if (resp.code != ErrorCode.OK) return resp.code.value;
    resolvePending(friends, requestId);
    // Roster sync arrives via the ACCEPTED notify, which the server sends to
    // both sides carrying the OTHER party's id.
    return resp.code.value;
  }

  /// Symmetric, idempotent delete on the server; the peer learns via notify.
  Future<int> removeFriend(String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.removeFriend,
        RemoveFriendRequest(
          userId: userId,
          friendUserId: targetUserId,
        ));
    if (resp.code == ErrorCode.OK) removeFriendStore(friends, targetUserId);
    return resp.code.value;
  }

  /// Blocking severs the friendship on the server (both ways).
  Future<int> blockUser(String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.blockUser,
        BlockUserRequest(
          userId: userId,
          targetUserId: targetUserId,
        ));
    if (resp.code == ErrorCode.OK) removeFriendStore(friends, targetUserId);
    return resp.code.value;
  }

  Future<int> unblockUser(String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.unblockUser,
        UnblockUserRequest(
          userId: userId,
          targetUserId: targetUserId,
        ));
    return resp.code.value;
  }

  void start() {
    if (_unsubs.isNotEmpty) return;
    _unsubs.addAll([
      conn.onNotify(MsgID.PRESENCE_NOTIFY, onPresenceNotify),
      conn.onNotify(MsgID.FRIEND_REQUEST_NOTIFY, onFriendRequestNotify),
      conn.onNotify(MsgID.FRIEND_ACCEPTED_NOTIFY, onFriendAcceptedNotify),
      conn.onNotify(MsgID.FRIEND_REMOVED_NOTIFY, onFriendRemovedNotify),
    ]);
  }

  void onPresenceNotify(Uint8List body) {
    PresenceNotify notify;
    try {
      notify = PresenceNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    setPresence(presence, notify.userId, notify.status, notify.statusMessage,
        DateTime.now().millisecondsSinceEpoch);
  }

  void onFriendRequestNotify(Uint8List body) {
    FriendRequestNotify notify;
    try {
      notify = FriendRequestNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    addPendingIn(friends, notify.requestId, notify.fromUserId);
  }

  void onFriendAcceptedNotify(Uint8List body) {
    FriendAcceptedNotify notify;
    try {
      notify = FriendAcceptedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    // user_id is always the OTHER party of the new friendship. A self echo
    // would mean "friend ourselves" — impossible from the current server,
    // but the guard is cheap and keeps a broken peer from poisoning us.
    if (notify.userId == auth.value.userId) return;
    addFriend(friends, notify.userId);
  }

  void onFriendRemovedNotify(Uint8List body) {
    FriendRemovedNotify notify;
    try {
      notify = FriendRemovedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    removeFriendStore(friends, notify.userId);
  }
}
