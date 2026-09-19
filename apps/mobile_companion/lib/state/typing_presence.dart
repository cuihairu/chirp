import 'package:chirp_proto/chirp_proto.dart';

import 'store.dart';

/// Typing indicators, per channel per user, timestamped on arrival. There is
/// no server-side stop guarantee beyond the explicit stop event, so the UI
/// treats entries older than TTL as idle (the server also broadcasts the stop
/// and rate-limits starts to one per ~3s).
const typingTtlMs = 6000;

class TypingState {
  const TypingState({required this.byChannel});

  /// channelKey → userId → last event timestamp (ms, client clock).
  final Map<String, Map<String, int>> byChannel;
}

Store<TypingState> createTypingStore() =>
    Store<TypingState>(const TypingState(byChannel: {}));

void setTyping(
    Store<TypingState> store, String channelKey, String userId, int at) {
  store.update((prev) => TypingState(byChannel: {
        ...prev.byChannel,
        channelKey: {...(prev.byChannel[channelKey] ?? const {}), userId: at},
      }));
}

void clearTyping(Store<TypingState> store, String channelKey, String userId) {
  store.update((prev) {
    final channel = {...(prev.byChannel[channelKey] ?? const <String, int>{})};
    if (!channel.containsKey(userId)) return prev;
    channel.remove(userId);
    return TypingState(byChannel: {...prev.byChannel, channelKey: channel});
  });
}

/// Users seen typing in this channel within the TTL (self already excluded).
List<String> typingUsersOf(TypingState state, String channelKey, int now) {
  final channel = state.byChannel[channelKey] ?? const <String, int>{};
  return channel.entries
      .where((entry) => now - entry.value < typingTtlMs)
      .map((entry) => entry.key)
      .toList();
}

class PresenceEntry {
  const PresenceEntry({
    required this.status,
    required this.statusMessage,
    required this.at,
  });

  final PresenceStatus status;
  final String statusMessage;
  final int at;
}

/// Peer presence, keyed by user id. Entries are advisory snapshots: the
/// social service broadcasts PRESENCE_NOTIFY to friends, and the client
/// bulk-pulls GET_PRESENCE when the conversation list changes. There is no
/// server-side timeout guarantee, so stale entries age out in the UI (see
/// presenceFresh).
class PresenceState {
  const PresenceState({required this.byUser});

  final Map<String, PresenceEntry> byUser;
}

Store<PresenceState> createPresenceStore() =>
    Store<PresenceState>(const PresenceState(byUser: {}));

void setPresence(Store<PresenceState> store, String userId,
    PresenceStatus status, String statusMessage, int at) {
  store.update((prev) => PresenceState(byUser: {
        ...prev.byUser,
        userId:
            PresenceEntry(status: status, statusMessage: statusMessage, at: at),
      }));
}

PresenceEntry? presenceOf(PresenceState state, String userId) =>
    state.byUser[userId];

/// Presences older than this render as offline (missed disconnect notifies).
const presenceTtlMs = 70000;

bool presenceFresh(PresenceState state, String userId, int now) {
  final entry = state.byUser[userId];
  return entry != null && now - entry.at < presenceTtlMs;
}
