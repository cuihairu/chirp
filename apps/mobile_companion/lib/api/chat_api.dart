import 'dart:convert';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';

import '../protocol/chat_connection.dart';
import '../protocol/errors.dart';
import '../protocol/msg_map.dart' as specs;
import '../protocol/chirp_client.dart';
import '../state/auth_store.dart';
import '../state/conversation_store.dart';
import '../state/message_store.dart';
import '../state/models.dart';
import '../state/store.dart';
import '../state/typing_presence.dart';

class ChannelRef {
  const ChannelRef({
    required this.key,
    required this.kind,
    required this.channelId,
    required this.peerId,
  });

  final String key;
  final ConversationKind kind;
  final String channelId;
  final String peerId;
}

/// A live incoming message handed to notification surfaces after it lands in
/// the stores.
class IncomingMessage {
  const IncomingMessage({
    required this.channel,
    required this.fromUserId,
    required this.content,
    required this.messageId,
  });

  final ChannelRef channel;
  final String fromUserId;
  final String content;
  final String messageId;
}

int _pendingCounter = 0;

/// Chat feature api: owns the notify→store wiring and turns request/response
/// pairs into UI mutations. Port of the web companion's ChatApi.
class ChatApi {
  ChatApi({
    required this.conn,
    required this.auth,
    required this.conversations,
    required this.messages,
    required this.typing,
  });

  final ChatConnection conn;
  final AuthStore auth;
  final Store<ConversationState> conversations;
  final Store<MessageState> messages;
  final Store<TypingState> typing;

  /// The channel currently open on screen; it never accumulates local unread.
  String? _activeChannelKey;
  final List<void Function()> _unsubs = [];
  final Set<void Function(IncomingMessage)> _messageListeners = {};

  /// Subscribe to server pushes. Idempotent; call again after stop().
  void start() {
    if (_unsubs.isNotEmpty) return;
    _unsubs.addAll([
      conn.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, onChatMessage),
      conn.onNotify(MsgID.GROUP_MEMBER_JOINED_NOTIFY, (_) => refreshGroups()),
      conn.onNotify(MsgID.GROUP_MEMBER_LEFT_NOTIFY, (_) => refreshGroups()),
      conn.onNotify(MsgID.GROUP_MEMBER_KICKED_NOTIFY, onKickedNotify),
      conn.onNotify(MsgID.GROUP_UPDATED_NOTIFY, (_) => refreshGroups()),
      conn.onNotify(MsgID.MESSAGE_READ_NOTIFY, onReadNotify),
      conn.onNotify(MsgID.TYPING_INDICATOR_NOTIFY, onTypingNotify),
      conn.onNotify(
          MsgID.REACTION_ADDED_NOTIFY, (body) => onReactionNotify(body, true)),
      conn.onNotify(MsgID.REACTION_REMOVED_NOTIFY,
          (body) => onReactionNotify(body, false)),
      conn.onNotify(MsgID.MESSAGE_EDITED_NOTIFY, onEditedNotify),
      conn.onNotify(MsgID.MESSAGE_DELETED_NOTIFY, onDeletedNotify),
    ]);
  }

  void stop() {
    for (final off in _unsubs) {
      off();
    }
    _unsubs.clear();
  }

  /// Fires for every live CHAT_MESSAGE_NOTIFY after it lands in the stores —
  /// used by the local-notification surface. Returns the unsubscribe fn.
  void Function() onMessage(void Function(IncomingMessage) listener) {
    _messageListeners.add(listener);
    return () => _messageListeners.remove(listener);
  }

  void setActiveChannel(String? key) {
    _activeChannelKey = key;
  }

  /// The channel currently open on screen (notification suppression).
  String? get activeChannelKey => _activeChannelKey;

  Future<ErrorCode> login(String userId) async {
    // First entry (or a dead socket) must open the connection; already-open
    // sockets are left alone.
    if (conn.status != ConnStatus.connected) {
      await conn.connect();
    }
    final resp = await conn.request(
        specs.login,
        LoginRequest(
          token: userId,
          deviceId: auth.value.deviceId,
          platform: 'android',
          supportsMessageAck: true,
        ));
    if (resp.code != ErrorCode.OK) {
      // The login screen surfaces this; auth stays untouched.
      throw RequestError(RequestErrorKind.server, code: resp.code);
    }
    auth.update((prev) => prev.copyWith(
          userId: userId,
          loggedIn: true,
          kicked: false,
        ));
    conn.resetBackoff();
    start();
    return resp.code;
  }

  /// Server-side logout, then a plain disconnect.
  Future<void> logout() async {
    final userId = auth.value.userId;
    try {
      if (userId != null) {
        await conn.request(specs.logout, LogoutRequest(userId: userId));
      }
    } on RequestError {
      // The disconnect below is what matters; a failed LOGOUT is harmless.
    }
    stop();
    conn.disconnect();
    // Not copyWith: null means "keep the field" there, and logout must
    // actually clear the user id.
    auth.value = AuthState(
      userId: null,
      kicked: false,
      deviceId: auth.value.deviceId,
      loggedIn: false,
    );
  }

  /// Optimistic send: the message goes on screen immediately and is replaced
  /// (or failed) by the SEND_MESSAGE_RESP. TARGET_OFFLINE is NOT a failure —
  /// the server already queued the message for the recipient.
  Future<void> sendToChannel(ChannelRef channel, String text) async {
    final senderId = auth.value.userId;
    if (senderId == null) return;
    final clientId =
        'pending-${DateTime.now().millisecondsSinceEpoch.toRadixString(36)}-${_pendingCounter++}';
    final optimistic = ChatMessageView(
      messageId: clientId,
      clientId: clientId,
      senderId: senderId,
      channelKey: channel.key,
      channelType: channelTypeOf(channel.kind),
      channelId: channel.channelId,
      content: text,
      timestamp: DateTime.now().millisecondsSinceEpoch,
      pending: true,
    );
    addPendingMessage(messages, optimistic);

    SendMessageResponse resp;
    try {
      resp = await conn.request(
          specs.sendMessage,
          SendMessageRequest(
            senderId: senderId,
            // Group sends MUST leave receiver empty (chat_validation.cc rejects it).
            receiverId:
                channel.kind == ConversationKind.private ? channel.peerId : '',
            channelType: channelTypeOf(channel.kind),
            channelId: channel.channelId,
            msgType: MsgType.TEXT,
            content: Uint8List.fromList(utf8.encode(text)),
            clientTimestamp: Int64(DateTime.now().millisecondsSinceEpoch),
          ));
    } on RequestError {
      failPendingMessage(messages, channel.key, clientId);
      return;
    }
    if (resp.code == ErrorCode.OK || resp.code == ErrorCode.TARGET_OFFLINE) {
      final confirmedFull = ChatMessageView(
        messageId: resp.messageId.isNotEmpty ? resp.messageId : clientId,
        clientId: clientId,
        senderId: optimistic.senderId,
        channelKey: optimistic.channelKey,
        channelType: optimistic.channelType,
        channelId: optimistic.channelId,
        content: optimistic.content,
        timestamp: optimistic.timestamp,
        pending: false,
        queuedOffline: resp.code == ErrorCode.TARGET_OFFLINE,
      );
      appendMessage(messages, confirmedFull);
      // First send to a fresh peer/group: the tail shows up in the list here.
      _ensureConversation(
          channel,
          channel.kind == ConversationKind.private
              ? channel.peerId
              : channel.channelId);
      touchConversation(
          conversations, channel.key, text, confirmedFull.timestamp);
    } else {
      failPendingMessage(messages, channel.key, clientId);
    }
  }

  /// Load (or page) history; oldest first, newest last.
  Future<void> loadHistory(ChannelRef channel, {int? beforeTimestamp}) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    setLoadingHistory(messages, channel.key, true);
    try {
      final resp = await conn.request(
          specs.getHistory,
          GetHistoryRequest(
            userId: userId,
            channelType: channelTypeOf(channel.kind),
            channelId: channel.channelId,
            limit: 50,
            beforeTimestamp: Int64(beforeTimestamp ?? 0),
          ));
      if (resp.code != ErrorCode.OK) return;
      setHasMore(messages, channel.key, resp.hasMore);
      prependHistory(
        messages,
        channel.key,
        resp.messages.map((m) => _toView(m, channel)).toList(),
        resp.hasMore,
      );
    } finally {
      setLoadingHistory(messages, channel.key, false);
    }
  }

  Future<void> markRead(ChannelRef channel, {String messageId = ''}) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    try {
      await conn.request(
          specs.markRead,
          MarkReadRequest(
            userId: userId,
            channelType: channelTypeOf(channel.kind),
            channelId: channel.channelId,
            messageId: messageId,
            readTimestamp: Int64(DateTime.now().millisecondsSinceEpoch),
          ));
    } on RequestError {
      // Receipts are best-effort; the local unread badge already cleared.
    }
  }

  /// Typing is fire-and-forget (2208, no RESP); the UI does the throttling.
  void sendTyping(ChannelRef channel, bool isTyping) {
    final userId = auth.value.userId;
    if (userId == null) return;
    conn.send(
      MsgID.TYPING_INDICATOR_NOTIFY,
      Uint8List.fromList(TypingIndicator(
        channelId: channel.channelId,
        channelType: channelTypeOf(channel.kind),
        userId: userId,
        username: userId,
        isTyping: isTyping,
        timestamp: Int64(DateTime.now().millisecondsSinceEpoch),
      ).writeToBuffer()),
    );
  }

  /// Toggle own emoji on a message; the RESP aggregate is the truth.
  Future<void> addReaction(String messageId, String emoji) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    final resp = await conn.request(
        specs.addReaction,
        AddReactionRequest(
          messageId: messageId,
          userId: userId,
          emoji: emoji,
        ));
    if (resp.code != ErrorCode.OK || !resp.hasReaction()) return;
    setReaction(
        messages,
        messageId,
        MessageReactionView(
          emoji: resp.reaction.emoji,
          count: resp.reaction.count,
          mine: resp.reaction.hasReactedByMe() && resp.reaction.reactedByMe ||
              resp.reaction.userIds.contains(userId),
        ));
  }

  Future<void> removeReaction(String messageId, String emoji) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    final resp = await conn.request(
        specs.removeReaction,
        RemoveReactionRequest(
          messageId: messageId,
          userId: userId,
          emoji: emoji,
        ));
    if (resp.code != ErrorCode.OK) return;
    // REMOVE_REACTION_RESP carries no aggregate; the notify is excluded for
    // the actor, so decrement locally.
    applyReaction(messages, messageId, emoji, true, false);
  }

  /// Edits apply locally from the RESP: the notify excludes the editor.
  Future<void> editMessage(String messageId, String text) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    final resp = await conn.request(
        specs.editMessage,
        EditMessageRequest(
          messageId: messageId,
          userId: userId,
          newContent: Uint8List.fromList(utf8.encode(text)),
          editTimestamp: Int64(DateTime.now().millisecondsSinceEpoch),
        ));
    if (resp.code != ErrorCode.OK) return;
    applyEditById(messages, messageId, text);
  }

  /// Soft delete (is_hard_delete is admin-only server-side).
  Future<void> deleteMessage(String messageId) async {
    final userId = auth.value.userId;
    if (userId == null) return;
    final resp = await conn.request(
        specs.deleteMessage,
        DeleteMessageRequest(
          messageId: messageId,
          userId: userId,
          isHardDelete: false,
        ));
    if (resp.code != ErrorCode.OK) return;
    applyDeleteById(messages, messageId);
  }

  /// Group roster for the conversation list (title side-info and badges).
  Future<List<Conversation>> refreshGroups() async {
    final userId = auth.value.userId;
    if (userId == null) return const [];
    final resp = await conn.request(
        specs.getUserGroups, GetUserGroupsRequest(userId: userId));
    if (resp.code != ErrorCode.OK) return const [];
    final existing = conversations.value.conversations;
    final result = <Conversation>[];
    for (final group in resp.groups) {
      final key = groupKeyOf(group.groupId);
      Conversation? prev;
      for (final c in existing) {
        if (c.key == key) {
          prev = c;
          break;
        }
      }
      result.add(prev ??
          Conversation(
            kind: ConversationKind.group,
            key: key,
            channelId: group.groupId,
            peerId: group.groupId,
            title: group.groupName,
            ownerId: group.ownerId,
            unreadLocal: 0,
          ));
    }
    for (final conversation in result) {
      upsertConversation(conversations, conversation);
    }
    return result;
  }

  Future<List<GroupMember>> loadGroupMembers(String groupId) async {
    final resp = await conn.request(
        specs.getGroupMembers, GetGroupMembersRequest(groupId: groupId));
    return resp.code == ErrorCode.OK ? resp.members : const [];
  }

  /// INVITE adds directly server-side (no pending state) — "添加成员".
  Future<ErrorCode> inviteToGroup(String groupId, String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return ErrorCode.SESSION_EXPIRED;
    final resp = await conn.request(
        specs.inviteToGroup,
        InviteToGroupRequest(
          inviterId: userId,
          groupId: groupId,
          targetUserId: targetUserId,
        ));
    if (resp.code == ErrorCode.OK) await refreshGroups();
    return resp.code;
  }

  Future<ErrorCode> kickGroupMember(String groupId, String targetUserId) async {
    final userId = auth.value.userId;
    if (userId == null) return ErrorCode.SESSION_EXPIRED;
    final resp = await conn.request(
        specs.kickMember,
        KickMemberRequest(
          requesterId: userId,
          groupId: groupId,
          targetUserId: targetUserId,
        ));
    if (resp.code == ErrorCode.OK) await refreshGroups();
    return resp.code;
  }

  /// Leave and drop the local cache: the MEMBER_LEFT notify excludes the actor.
  Future<ErrorCode> leaveGroup(String groupId) async {
    final userId = auth.value.userId;
    if (userId == null) return ErrorCode.SESSION_EXPIRED;
    final resp = await conn.request(
        specs.leaveGroup,
        LeaveGroupRequest(
          userId: userId,
          groupId: groupId,
        ));
    if (resp.code == ErrorCode.OK) {
      final key = groupKeyOf(groupId);
      removeConversation(conversations, key);
      clearChannel(messages, key);
      if (_activeChannelKey == key) _activeChannelKey = null;
    }
    return resp.code;
  }

  /// 2120: someone was kicked. If it was me, forget the group locally.
  void onKickedNotify(Uint8List body) {
    GroupMemberKickedNotify notify;
    try {
      notify = GroupMemberKickedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    final key = groupKeyOf(notify.groupId);
    if (notify.userId == auth.value.userId) {
      removeConversation(conversations, key);
      clearChannel(messages, key);
      if (_activeChannelKey == key) _activeChannelKey = null;
    } else {
      refreshGroups();
    }
  }

  Future<String?> createGroup(String name, {String description = ''}) async {
    final userId = auth.value.userId;
    if (userId == null) return null;
    final resp = await conn.request(
        specs.createGroup,
        CreateGroupRequest(
          creatorId: userId,
          groupName: name,
          description: description,
        ));
    if (resp.code != ErrorCode.OK) return null;
    await refreshGroups();
    return resp.groupId;
  }

  void onChatMessage(Uint8List body) {
    ChatMessage msg;
    try {
      msg = ChatMessage.fromBuffer(body);
    } catch (_) {
      return;
    }
    final selfId = auth.value.userId ?? '';
    final key = msg.channelType == ChannelType.PRIVATE
        ? privateKey(msg.senderId, selfId)
        : groupKeyOf(msg.channelId);
    // We logged in with supportsMessageAck=true, so every live push must be
    // acked or the server rolls the delivery back into the offline queue
    // after 10s. Fire-and-forget (2209 has no response frame).
    conn.send(
      MsgID.MESSAGE_ACK,
      Uint8List.fromList(MessageAck(
        messageId: msg.messageId,
        userId: selfId,
        receivedAt: Int64(DateTime.now().millisecondsSinceEpoch),
      ).writeToBuffer()),
    );
    final channel = ChannelRef(
      key: key,
      kind: msg.channelType == ChannelType.PRIVATE
          ? ConversationKind.private
          : ConversationKind.group,
      channelId: msg.channelId,
      peerId:
          msg.channelType == ChannelType.PRIVATE ? msg.senderId : msg.channelId,
    );
    appendMessage(messages, _toView(msg, channel));
    _ensureConversation(
        channel,
        channel.kind == ConversationKind.private
            ? msg.senderId
            : msg.channelId);
    touchConversation(
        conversations, key, utf8.decode(msg.content), msg.timestamp.toInt());
    if (key != _activeChannelKey) {
      bumpUnread(conversations, key);
    }
    final content = utf8.decode(msg.content);
    for (final listener in List.of(_messageListeners)) {
      listener(IncomingMessage(
        channel: channel,
        fromUserId: msg.senderId,
        content: content,
        messageId: msg.messageId,
      ));
    }
  }

  /// A private message from someone we never talked to joins the list here:
  /// the server has no private-conversation listing API (P1 aggregation is
  /// the authoritative fix), so the client fills the gap locally.
  void _ensureConversation(ChannelRef channel, String title) {
    final exists =
        conversations.value.conversations.any((c) => c.key == channel.key);
    if (!exists) {
      upsertConversation(
        conversations,
        Conversation(
          kind: channel.kind,
          key: channel.key,
          channelId: channel.channelId,
          peerId: channel.peerId,
          title: title,
          unreadLocal: 0,
        ),
      );
    }
  }

  void onReadNotify(Uint8List body) {
    MessageReadNotify notify;
    try {
      notify = MessageReadNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    final selfId = auth.value.userId ?? '';
    if (notify.readerUserId == selfId) return; // own echoes are meaningless
    final key = notify.channelType == ChannelType.PRIVATE
        ? privateKey(notify.readerUserId, selfId)
        : groupKeyOf(notify.channelId);
    setReadCursor(messages, key, notify.readerUserId, notify.messageId);
  }

  void onTypingNotify(Uint8List body) {
    TypingIndicator indicator;
    try {
      indicator = TypingIndicator.fromBuffer(body);
    } catch (_) {
      return;
    }
    final selfId = auth.value.userId ?? '';
    if (indicator.userId == selfId) return;
    final key = indicator.channelType == ChannelType.PRIVATE
        ? privateKey(indicator.userId, selfId)
        : groupKeyOf(indicator.channelId);
    if (indicator.isTyping) {
      setTyping(
          typing, key, indicator.userId, DateTime.now().millisecondsSinceEpoch);
    } else {
      clearTyping(typing, key, indicator.userId);
    }
  }

  void onReactionNotify(Uint8List body, bool added) {
    try {
      if (added) {
        final notify = ReactionAddedNotify.fromBuffer(body);
        applyReaction(messages, notify.messageId, notify.emoji,
            notify.userId == auth.value.userId, added);
      } else {
        final notify = ReactionRemovedNotify.fromBuffer(body);
        applyReaction(messages, notify.messageId, notify.emoji,
            notify.userId == auth.value.userId, added);
      }
    } catch (_) {
      return;
    }
  }

  void onEditedNotify(Uint8List body) {
    MessageEditedNotify notify;
    try {
      notify = MessageEditedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    applyEditById(messages, notify.messageId, utf8.decode(notify.newContent));
  }

  void onDeletedNotify(Uint8List body) {
    MessageDeletedNotify notify;
    try {
      notify = MessageDeletedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    applyDeleteById(messages, notify.messageId);
  }
}

ChatMessageView _toView(ChatMessage msg, ChannelRef channel) => ChatMessageView(
      messageId: msg.messageId,
      senderId: msg.senderId,
      channelKey: channel.key,
      channelType: msg.channelType,
      channelId: msg.channelId,
      content: utf8.decode(msg.content),
      timestamp: msg.timestamp.toInt(),
      pending: false,
    );

/// ChannelRef for a stored conversation key; private peers resolve via the
/// sorted-pair channel id ("a|b" → the other side).
ChannelRef channelRefOf(String key, String selfId) {
  final parsed = conversationOf(key);
  return ChannelRef(
    key: key,
    kind: parsed.kind,
    channelId: parsed.channelId,
    peerId: parsed.kind == ConversationKind.group
        ? parsed.channelId
        : (parsed.channelId
            .split('|')
            .firstWhere((id) => id != selfId, orElse: () => parsed.channelId)),
  );
}
