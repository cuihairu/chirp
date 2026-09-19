import 'models.dart';
import 'store.dart';

class MessageState {
  const MessageState({
    required this.byChannel,
    required this.hasMore,
    required this.loadingHistory,
    required this.readCursors,
  });

  /// channelKey → messages ordered oldest → newest.
  final Map<String, List<ChatMessageView>> byChannel;

  /// Older history exists on the server for this channel.
  final Map<String, bool> hasMore;
  final Map<String, bool> loadingHistory;

  /// channelKey → userId → last message id that user has read.
  final Map<String, Map<String, String>> readCursors;
}

Store<MessageState> createMessageStore() =>
    Store<MessageState>(const MessageState(
      byChannel: {},
      hasMore: {},
      loadingHistory: {},
      readCursors: {},
    ));

List<ChatMessageView> messagesOf(MessageState state, String key) =>
    state.byChannel[key] ?? const [];

/// Append live/confirmed messages and drop duplicates by server id. The
/// sender gets no echo of its own messages: its copies arrive through
/// SEND_MESSAGE_RESP and are matched by clientId first.
void appendMessage(Store<MessageState> store, ChatMessageView message) {
  store.update((prev) {
    final list =
        prev.byChannel[message.channelKey] ?? const <ChatMessageView>[];
    if (list.any((m) => m.messageId == message.messageId)) return prev;
    // An optimistic placeholder for the same message is replaced, not added;
    // without a match the message is appended to the tail.
    final replaced = message.clientId == null
        ? list
        : list
            .map((m) => m.clientId == message.clientId ? message : m)
            .toList();
    return _withChannel(prev, message.channelKey,
        identical(replaced, list) ? [...list, message] : replaced);
  });
}

/// Upsert the optimistic placeholder for a send awaiting its RESP.
void addPendingMessage(Store<MessageState> store, ChatMessageView message) {
  store.update((prev) {
    final list =
        prev.byChannel[message.channelKey] ?? const <ChatMessageView>[];
    return _withChannel(prev, message.channelKey, [...list, message]);
  });
}

/// Flip a pending send to failed (RESP error or timeout).
void failPendingMessage(
    Store<MessageState> store, String channelKey, String clientId) {
  store.update((prev) => _withChannel(
        prev,
        channelKey,
        (prev.byChannel[channelKey] ?? const <ChatMessageView>[]).map((m) {
          if (m.clientId != clientId) return m;
          return m.copyWith(pending: false, failed: true);
        }).toList(),
      ));
}

/// Prepend a page of older history; `hasMore` comes from the server.
void prependHistory(Store<MessageState> store, String channelKey,
    List<ChatMessageView> messages, bool hasMore) {
  store.update((prev) {
    final existing = prev.byChannel[channelKey] ?? const <ChatMessageView>[];
    final known = existing.map((m) => m.messageId).toSet();
    final fresh = messages.where((m) => !known.contains(m.messageId)).toList();
    return MessageState(
      byChannel: {
        ...prev.byChannel,
        channelKey: [...fresh, ...existing]
      },
      hasMore: {...prev.hasMore, channelKey: hasMore},
      loadingHistory: prev.loadingHistory,
      readCursors: prev.readCursors,
    );
  });
}

void setLoadingHistory(
    Store<MessageState> store, String channelKey, bool loading) {
  store.update((prev) => MessageState(
        byChannel: prev.byChannel,
        hasMore: prev.hasMore,
        loadingHistory: {...prev.loadingHistory, channelKey: loading},
        readCursors: prev.readCursors,
      ));
}

void setHasMore(
    Store<MessageState> store, String channelKey, bool hasMoreFlag) {
  store.update((prev) => MessageState(
        byChannel: prev.byChannel,
        hasMore: {...prev.hasMore, channelKey: hasMoreFlag},
        loadingHistory: prev.loadingHistory,
        readCursors: prev.readCursors,
      ));
}

/// Record a MESSAGE_READ_NOTIFY: readerUserId has read up to messageId.
void setReadCursor(Store<MessageState> store, String channelKey,
    String readerUserId, String messageId) {
  store.update((prev) {
    final channel = prev.readCursors[channelKey] ?? const <String, String>{};
    if (channel[readerUserId] == messageId) return prev;
    return MessageState(
      byChannel: prev.byChannel,
      hasMore: prev.hasMore,
      loadingHistory: prev.loadingHistory,
      readCursors: {
        ...prev.readCursors,
        channelKey: {...channel, readerUserId: messageId},
      },
    );
  });
}

String? readCursorOf(
        MessageState state, String channelKey, String readerUserId) =>
    state.readCursors[channelKey]?[readerUserId];

/// Forget a channel's local cache (left the group, kicked from it).
void clearChannel(Store<MessageState> store, String channelKey) {
  store.update((prev) {
    if (!prev.byChannel.containsKey(channelKey)) return prev;
    final byChannel = {...prev.byChannel}..remove(channelKey);
    final hasMore = {...prev.hasMore}..remove(channelKey);
    final loadingHistory = {...prev.loadingHistory}..remove(channelKey);
    return MessageState(
      byChannel: byChannel,
      hasMore: hasMore,
      loadingHistory: loadingHistory,
      readCursors: prev.readCursors,
    );
  });
}

/// Apply a reaction RESP/notify by locating the message id across channels
/// (message ids are globally unique; the notify carries no channel type, so
/// scanning is the honest option at companion scale).
void applyReaction(Store<MessageState> store, String messageId, String emoji,
    bool mine, bool added) {
  store.update((prev) {
    var changed = false;
    final byChannel = <String, List<ChatMessageView>>{};
    prev.byChannel.forEach((key, list) {
      byChannel[key] = list.map((m) {
        if (m.messageId != messageId) return m;
        changed = true;
        final reactions = {...m.reactions};
        final current = reactions[emoji] ??
            MessageReactionView(emoji: emoji, count: 0, mine: false);
        var mineNow = current.mine;
        var count = current.count;
        if (added) {
          count += 1;
          if (mine) mineNow = true;
        } else {
          count = count > 0 ? count - 1 : 0;
          if (mine) mineNow = false;
        }
        if (count == 0) {
          reactions.remove(emoji);
        } else {
          reactions[emoji] = current.copyWith(count: count, mine: mineNow);
        }
        // Hand the map through even when empty: copyWith treats null as
        // "keep", so only a real (possibly empty) map replaces the old one.
        return m.copyWith(reactions: reactions);
      }).toList();
    });
    return changed
        ? MessageState(
            byChannel: byChannel,
            hasMore: prev.hasMore,
            loadingHistory: prev.loadingHistory,
            readCursors: prev.readCursors,
          )
        : prev;
  });
}

/// Replace a reaction aggregate wholesale (from ADD_REACTION_RESP).
void setReaction(
    Store<MessageState> store, String messageId, MessageReactionView reaction) {
  store.update((prev) {
    var changed = false;
    final byChannel = <String, List<ChatMessageView>>{};
    prev.byChannel.forEach((key, list) {
      byChannel[key] = list.map((m) {
        if (m.messageId != messageId) return m;
        changed = true;
        final reactions = {...m.reactions, reaction.emoji: reaction};
        return m.copyWith(reactions: reactions);
      }).toList();
    });
    return changed
        ? MessageState(
            byChannel: byChannel,
            hasMore: prev.hasMore,
            loadingHistory: prev.loadingHistory,
            readCursors: prev.readCursors,
          )
        : prev;
  });
}

/// Edit/delete by message id, wherever it lives (notifies carry no channel type).
void applyEditById(
    Store<MessageState> store, String messageId, String content) {
  store.update((prev) {
    var changed = false;
    final byChannel = <String, List<ChatMessageView>>{};
    prev.byChannel.forEach((key, list) {
      byChannel[key] = list.map((m) {
        if (m.messageId != messageId) return m;
        changed = true;
        return m.copyWith(content: content, edited: true);
      }).toList();
    });
    return changed
        ? MessageState(
            byChannel: byChannel,
            hasMore: prev.hasMore,
            loadingHistory: prev.loadingHistory,
            readCursors: prev.readCursors,
          )
        : prev;
  });
}

void applyDeleteById(Store<MessageState> store, String messageId) {
  store.update((prev) {
    var changed = false;
    final byChannel = <String, List<ChatMessageView>>{};
    prev.byChannel.forEach((key, list) {
      byChannel[key] = list.map((m) {
        if (m.messageId != messageId) return m;
        changed = true;
        return m.copyWith(deleted: true, content: '');
      }).toList();
    });
    return changed
        ? MessageState(
            byChannel: byChannel,
            hasMore: prev.hasMore,
            loadingHistory: prev.loadingHistory,
            readCursors: prev.readCursors,
          )
        : prev;
  });
}

MessageState _withChannel(
    MessageState prev, String channelKey, List<ChatMessageView> list) {
  return MessageState(
    byChannel: {...prev.byChannel, channelKey: list},
    hasMore: prev.hasMore,
    loadingHistory: prev.loadingHistory,
    readCursors: prev.readCursors,
  );
}
