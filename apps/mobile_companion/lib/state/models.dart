import 'package:chirp_proto/chirp_proto.dart';

/// UI-layer models. Channel keys are local-only: the server keys private
/// history by the sorted "a|b" pair and groups by group id, and the app needs
/// one namespace for conversations across both.

enum ConversationKind { private, group }

class Conversation {
  const Conversation({
    required this.kind,
    required this.key,
    required this.channelId,
    required this.peerId,
    required this.title,
    required this.unreadLocal,
    this.ownerId,
    this.lastMessagePreview,
    this.lastMessageAt,
  });

  final ConversationKind kind;

  /// 'p:' + sorted pair for private, 'g:' + group id for groups.
  final String key;

  /// Server-side channel id ('a|b' or group id).
  final String channelId;

  /// Peer user id (private) or group id (group).
  final String peerId;
  final String title;

  /// Group owner (groups only) — gates the kick affordance in the UI.
  final String? ownerId;

  /// Tail of the conversation, kept in sync by message_store.
  final String? lastMessagePreview;
  final int? lastMessageAt;

  /// Local unread badge count; bumps on notifies outside the open channel.
  final int unreadLocal;

  Conversation copyWith({
    String? title,
    String? ownerId,
    String? lastMessagePreview,
    int? lastMessageAt,
    int? unreadLocal,
  }) =>
      Conversation(
        kind: kind,
        key: key,
        channelId: channelId,
        peerId: peerId,
        title: title ?? this.title,
        ownerId: ownerId ?? this.ownerId,
        unreadLocal: unreadLocal ?? this.unreadLocal,
        lastMessagePreview: lastMessagePreview ?? this.lastMessagePreview,
        lastMessageAt: lastMessageAt ?? this.lastMessageAt,
      );
}

class MessageReactionView {
  const MessageReactionView({
    required this.emoji,
    required this.count,
    required this.mine,
  });

  /// Unicode emoji.
  final String emoji;
  final int count;

  /// Whether the logged-in user is among the reactors.
  final bool mine;

  MessageReactionView copyWith({int? count, bool? mine}) => MessageReactionView(
        emoji: emoji,
        count: count ?? this.count,
        mine: mine ?? this.mine,
      );
}

class ChatMessageView {
  const ChatMessageView({
    required this.messageId,
    required this.senderId,
    required this.channelKey,
    required this.channelType,
    required this.channelId,
    required this.content,
    required this.timestamp,
    required this.pending,
    this.clientId,
    this.failed = false,
    this.queuedOffline = false,
    this.edited = false,
    this.deleted = false,
    this.reactions = const {},
  });

  /// Server message id; optimistic sends carry a local id until confirmed.
  final String messageId;

  /// Present while the message is an optimistic placeholder.
  final String? clientId;
  final String senderId;
  final String channelKey;
  final ChannelType channelType;
  final String channelId;

  /// Decoded text (phase one is TEXT-only).
  final String content;
  final int timestamp;

  /// Optimistic, not yet acknowledged by a SEND_MESSAGE_RESP.
  final bool pending;

  /// Server rejected the send (RESP code != OK).
  final bool failed;

  /// TARGET_OFFLINE is still a delivery: queued server-side.
  final bool queuedOffline;
  final bool edited;
  final bool deleted;

  /// Emoji → aggregate, updated by reaction RESP/notify.
  final Map<String, MessageReactionView> reactions;

  ChatMessageView copyWith({
    String? content,
    bool? pending,
    bool? failed,
    bool? edited,
    bool? deleted,
    Map<String, MessageReactionView>? reactions,
  }) =>
      ChatMessageView(
        messageId: messageId,
        senderId: senderId,
        channelKey: channelKey,
        channelType: channelType,
        channelId: channelId,
        content: content ?? this.content,
        timestamp: timestamp,
        pending: pending ?? this.pending,
        clientId: clientId,
        failed: failed ?? this.failed,
        queuedOffline: queuedOffline,
        edited: edited ?? this.edited,
        deleted: deleted ?? this.deleted,
        reactions: reactions ?? this.reactions,
      );
}

const privatePrefix = 'p:';
const groupPrefix = 'g:';

String privateKey(String a, String b) {
  final pair = [a, b]..sort();
  return privatePrefix + pair.join('|');
}

String groupKeyOf(String groupId) => groupPrefix + groupId;

({ConversationKind kind, String channelId}) conversationOf(String key) {
  if (key.startsWith(privatePrefix)) {
    return (
      kind: ConversationKind.private,
      channelId: key.substring(privatePrefix.length),
    );
  }
  return (
    kind: ConversationKind.group,
    channelId: key.substring(groupPrefix.length),
  );
}

ChannelType channelTypeOf(ConversationKind kind) =>
    kind == ConversationKind.private ? ChannelType.PRIVATE : ChannelType.GUILD;
