import 'models.dart';
import 'store.dart';

class ConversationState {
  const ConversationState({required this.conversations});

  /// Ordered: most recent activity first.
  final List<Conversation> conversations;
}

Store<ConversationState> createConversationStore() =>
    Store<ConversationState>(const ConversationState(conversations: []));

/// Insert or update a conversation and re-sort by recent activity.
void upsertConversation(
    Store<ConversationState> store, Conversation conversation) {
  store.update((prev) {
    final others =
        prev.conversations.where((c) => c.key != conversation.key).toList();
    final existing = prev.conversations
        .where((c) => c.key == conversation.key)
        .toList()
        .firstOrNull;
    final merged = existing == null
        ? conversation
        : Conversation(
            kind: conversation.kind,
            key: conversation.key,
            channelId: conversation.channelId,
            peerId: conversation.peerId,
            title: conversation.title,
            ownerId: conversation.ownerId ?? existing.ownerId,
            unreadLocal: existing.unreadLocal,
            lastMessagePreview:
                conversation.lastMessagePreview ?? existing.lastMessagePreview,
            lastMessageAt: conversation.lastMessageAt ?? existing.lastMessageAt,
          );
    return ConversationState(
        conversations: sortConversations([merged, ...others]));
  });
}

/// Tail update only (no re-insert for unknown conversations).
void touchConversation(
    Store<ConversationState> store, String key, String preview, int at) {
  store.update((prev) => ConversationState(
        conversations: sortConversations(prev.conversations.map((c) {
          if (c.key != key) return c;
          return c.copyWith(lastMessagePreview: preview, lastMessageAt: at);
        }).toList()),
      ));
}

void bumpUnread(Store<ConversationState> store, String key) {
  store.update((prev) => ConversationState(
        conversations: prev.conversations.map((c) {
          if (c.key != key) return c;
          return c.copyWith(unreadLocal: c.unreadLocal + 1);
        }).toList(),
      ));
}

void clearUnread(Store<ConversationState> store, String key) {
  store.update((prev) => ConversationState(
        conversations: prev.conversations.map((c) {
          if (c.key != key) return c;
          return c.copyWith(unreadLocal: 0);
        }).toList(),
      ));
}

/// Drop a conversation entirely (left the group, or kicked from it).
void removeConversation(Store<ConversationState> store, String key) {
  store.update((prev) {
    if (!prev.conversations.any((c) => c.key == key)) return prev;
    return ConversationState(
        conversations: prev.conversations.where((c) => c.key != key).toList());
  });
}

List<Conversation> sortConversations(List<Conversation> conversations) {
  final copy = List.of(conversations);
  copy.sort((a, b) {
    final byTime = (b.lastMessageAt ?? 0).compareTo(a.lastMessageAt ?? 0);
    return byTime != 0 ? byTime : a.key.compareTo(b.key);
  });
  return copy;
}

/// firstOrNull lives in package:collection; a private twin avoids the dep.
extension _FirstOrNull<T> on Iterable<T> {
  T? get firstOrNull => isEmpty ? null : first;
}
