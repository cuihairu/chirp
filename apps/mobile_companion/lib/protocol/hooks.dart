import 'package:chirp_proto/chirp_proto.dart';

import 'chirp_client.dart';

/// Five hook interfaces for the chat pipeline — the Dart face of the same
/// contract the C++ core (sdks/core/include/chirp/*), the Unity SDK
/// (sdks/unity) and the web protocol layer ship:
///
/// - [MessageInterceptor]: rewrite/audit points around send & receive.
///   Returning false from onBeforeSend/onBeforeReceive drops the message;
///   the receive side drop also skips the archive and listeners.
/// - [AuthProvider]: token sourcing for login + one renewal on AUTH_FAILED.
/// - [MessageStore]: local archive; the pipeline saves both directions.
/// - [ChatEventListener]: lifecycle fan-out.
/// - [CommandHandler]: local '/'-command routing.
///
/// Dart has no interface default methods, so each hook is an abstract class
/// with no-op concrete bodies: extend it and override only the hooks you
/// need — an untouched override behaves exactly like an absent hook (the
/// C++/C# "zero hooks = zero change" rule).

/// Send parameters for [ChatPipeline.send] (C++ SendOptions / C# SendOptions).
class SendOptions {
  const SendOptions({
    required this.channelType,
    this.channelId,
    this.receiverId,
    this.replyToMessageId,
    this.msgType,
  });

  final ChannelType channelType;

  /// Required for every non-private channel; private derives the sorted pair.
  final String? channelId;

  /// Required for private; the wire channel id is the sorted "a|b" pair.
  final String? receiverId;

  /// Quoted message id; empty/absent = not a reply.
  final String? replyToMessageId;
  final MsgType? msgType;
}

abstract class MessageInterceptor {
  /// false = the message never reaches the wire.
  bool onBeforeSend(SendMessageRequest request) => true;
  void onAfterSend(SendMessageRequest request) {}

  /// false = dropped for the archive, the listeners and onAfterReceive.
  bool onBeforeReceive(ChatMessage message) => true;
  void onAfterReceive(ChatMessage message) {}
}

abstract class AuthProvider {
  /// Used when no explicit token is passed to login().
  String getToken();

  /// AUTH_FAILED gives this one chance to hand back a fresh token (the
  /// pipeline retries the login round once). Returning null ends the login
  /// with the failure — at most one renewal per login chain, identical to
  /// C++ OnTokenExpired / C# RenewTokenAsync.
  Future<String?> renewToken() async => null;

  /// Terminal auth outcome: success or final failure.
  void onAuthResult(ErrorCode code, String userId) {}
}

abstract class MessageStore {
  /// Called for both directions (sent copies carry an empty messageId).
  void save(ChatMessage message);

  /// Newest first; beforeTimestamp 0/null = no bound.
  List<ChatMessage> load(
    ChannelType channelType,
    String channelId,
    int limit, {
    int? beforeTimestamp,
  });

  /// Without an implementation the pipeline reports 0 (C++/C# parity).
  int getUnreadCount(ChannelType channelType, String channelId) => 0;

  void markRead(ChannelType channelType, String channelId, String messageId) {}

  void cleanup(int olderThanMs) {}
}

/// In-memory archive keyed by `$channelType|$channelId`. Newest-first loads,
/// oldest-first eviction beyond the cap — the C#/C++/TS MemoryMessageStore
/// semantics. markRead/getUnreadCount are not tracked (0, like the reference).
class MemoryMessageStore extends MessageStore {
  MemoryMessageStore({this.maxPerChannel = 200});

  /// 0 = unbounded.
  final int maxPerChannel;

  final Map<String, List<ChatMessage>> _channels = {};

  @override
  void save(ChatMessage message) {
    final key = '${message.channelType.value}|${message.channelId}';
    final bucket = _channels.putIfAbsent(key, () => <ChatMessage>[]);
    bucket.add(message);
    if (maxPerChannel > 0 && bucket.length > maxPerChannel) {
      bucket.removeRange(0, bucket.length - maxPerChannel);
    }
  }

  @override
  List<ChatMessage> load(
    ChannelType channelType,
    String channelId,
    int limit, {
    int? beforeTimestamp,
  }) {
    final out = <ChatMessage>[];
    if (limit <= 0) return out;
    final bucket = _channels['${channelType.value}|$channelId'];
    if (bucket == null) return out;
    for (var i = bucket.length - 1; i >= 0 && out.length < limit; i--) {
      final message = bucket[i];
      if (beforeTimestamp == null ||
          beforeTimestamp == 0 ||
          message.timestamp.toInt() < beforeTimestamp) {
        out.add(message);
      }
    }
    return out;
  }

  @override
  void cleanup(int olderThanMs) {
    final deadKeys = <String>[];
    _channels.forEach((key, bucket) {
      bucket.removeWhere((m) => m.timestamp.toInt() < olderThanMs);
      if (bucket.isEmpty) deadKeys.add(key);
    });
    for (final key in deadKeys) {
      _channels.remove(key);
    }
  }
}

/// Lifecycle listener. onReconnecting/onReconnected/onMessageReceived are
/// wired by [ChatPipeline]; onLoginResult/onKicked/onConnectionStateChanged
/// are the terminal reports. Unread/presence/typing/marquee/announcement
/// have no trigger source on this plane yet — leave them untouched.
abstract class ChatEventListener {
  void onConnectionStateChanged(ConnStatus status) {}
  void onLoginResult(ErrorCode code, String userId) {}
  void onKicked(String reason) {}
  void onReconnecting(int attempt, int delayMs) {}
  void onReconnected() {}
  void onMessageReceived(ChatMessage message) {}
}

abstract class CommandHandler {
  /// Command word after the '/', matched in registration order.
  String get name;

  /// Display form; defaults to `/$name` like the C++/C# GetUsage default.
  String get usage => '/$name';

  /// false = the next handler with the same name gets a try.
  bool execute(String args, String senderId);
}
