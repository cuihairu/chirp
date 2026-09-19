import 'store.dart';

class PendingRequest {
  const PendingRequest({required this.requestId, required this.fromUserId});

  final String requestId;
  final String fromUserId;
}

/// Client-side mirror of the social plane's roster. The backend is
/// authoritative: login pulls the friend list and the incoming request queue
/// (GET_FRIEND_LIST / GET_PENDING_REQUESTS), and notifications keep the mirror
/// current. Only pendingOut lives purely client-side — the service has no
/// outgoing-request query yet, so an app restart drops that list.
class FriendState {
  const FriendState({
    required this.friends,
    required this.pendingIn,
    required this.pendingOut,
  });

  /// Accepted friends (user ids, sorted).
  final List<String> friends;

  /// Requests we received and have not answered.
  final List<PendingRequest> pendingIn;

  /// Requests we sent that nobody has answered yet.
  final List<String> pendingOut;
}

Store<FriendState> createFriendStore() => Store<FriendState>(const FriendState(
      friends: [],
      pendingIn: [],
      pendingOut: [],
    ));

void replaceFriends(Store<FriendState> store, List<String> ids) {
  final sorted = List.of(ids)..sort();
  store.update((prev) {
    final same = prev.friends.length == sorted.length &&
        Iterable<int>.generate(sorted.length)
            .every((i) => prev.friends[i] == sorted[i]);
    return same
        ? prev
        : FriendState(
            friends: sorted,
            pendingIn: prev.pendingIn,
            pendingOut: prev.pendingOut,
          );
  });
}

void replacePendingIn(Store<FriendState> store, List<PendingRequest> requests) {
  store.update((prev) => FriendState(
        friends: prev.friends,
        pendingIn: requests,
        pendingOut: prev.pendingOut,
      ));
}

void addFriend(Store<FriendState> store, String userId) {
  store.update((prev) => prev.friends.contains(userId)
      ? prev
      : FriendState(
          friends: [...prev.friends, userId]..sort(),
          pendingIn: prev.pendingIn,
          pendingOut: prev.pendingOut,
        ));
}

void removeFriendStore(Store<FriendState> store, String userId) {
  store.update((prev) => prev.friends.contains(userId)
      ? FriendState(
          friends: prev.friends.where((f) => f != userId).toList(),
          pendingIn: prev.pendingIn,
          pendingOut: prev.pendingOut,
        )
      : prev);
}

void addPendingOut(Store<FriendState> store, String userId) {
  store.update((prev) =>
      prev.pendingOut.contains(userId) || prev.friends.contains(userId)
          ? prev
          : FriendState(
              friends: prev.friends,
              pendingIn: prev.pendingIn,
              pendingOut: [...prev.pendingOut, userId]..sort(),
            ));
}

void addPendingIn(
    Store<FriendState> store, String requestId, String fromUserId) {
  store.update((prev) => prev.pendingIn.any((r) => r.requestId == requestId)
      ? prev
      : FriendState(
          friends: prev.friends,
          pendingIn: [
            ...prev.pendingIn,
            PendingRequest(requestId: requestId, fromUserId: fromUserId),
          ],
          pendingOut: prev.pendingOut,
        ));
}

void resolvePending(Store<FriendState> store, String requestId) {
  store.update((prev) => FriendState(
        friends: prev.friends,
        pendingIn:
            prev.pendingIn.where((r) => r.requestId != requestId).toList(),
        pendingOut: prev.pendingOut,
      ));
}

String? fromUserIdOf(FriendState state, String requestId) {
  for (final request in state.pendingIn) {
    if (request.requestId == requestId) return request.fromUserId;
  }
  return null;
}
