import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_proto/proto/auth.pb.dart' as auth;
import 'package:chirp_proto/proto/chat.pb.dart' as chat;
import 'package:fixnum/fixnum.dart';

import 'chat_connection.dart';
import 'chirp_client.dart';
import 'errors.dart';
import 'hooks.dart';
import 'msg_map.dart' as specs;

/// The hook host for the Dart plane, structurally the sibling of ChatClient
/// on C++ (sdks/core), Unity/.NET and the web protocol layer's ChatPipeline:
/// command routing, interceptor rewrite/drop, local archive and lifecycle
/// listeners around a plain request/notify connection.
///
/// Threading: everything runs on the Dart event loop, so the C++/C# "io
/// thread → marshal to game thread" contract collapses to plain calls here.
/// Ordering guarantees are preserved exactly: an onBeforeReceive drop kills
/// the archive, the listeners and onAfterReceive; other notify subscribers
/// still see the original wire body, untouched by interceptor rewrites.
///
/// Messages that never reach the wire throw RequestError(RequestErrorKind
/// .blocked) — command handled locally, unknown command, interceptor drop —
/// identical to the C# SendMessageAsync contract. A returned response may
/// still carry a non-OK code (rate limit, invalid param…); read resp.code.
class ChatPipeline {
  ChatPipeline({
    required ChatConnection conn,
    required String Function() selfId,
    required String Function() deviceId,
  })  : _conn = conn,
        _selfId = selfId,
        _deviceId = deviceId;

  final ChatConnection _conn;
  final String Function() _selfId;
  final String Function() _deviceId;

  MessageInterceptor? _interceptor;
  AuthProvider? _provider;
  MessageStore? _store;
  final List<ChatEventListener> _listeners = [];
  final List<CommandHandler> _commands = [];
  final List<void Function()> _unsubs = [];

  // ---- hook registration (C++/C# setter parity) ----------------------------

  set interceptor(MessageInterceptor? value) => _interceptor = value;
  set provider(AuthProvider? value) => _provider = value;
  set store(MessageStore? value) => _store = value;

  /// Returns the unsubscribe function.
  void Function() addListener(ChatEventListener listener) {
    _listeners.add(listener);
    return () => _listeners.remove(listener);
  }

  /// Returns the unsubscribe function.
  void Function() registerCommand(CommandHandler handler) {
    _commands.add(handler);
    return () => _commands.remove(handler);
  }

  // ---- lifecycle wiring ----------------------------------------------------

  /// Idempotent; call again after [stop].
  void start() {
    if (_unsubs.isNotEmpty) return;
    _unsubs
      ..add(_conn.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, _onIncoming))
      ..add(_conn.onNotify(MsgID.KICK_NOTIFY, _onKickBody))
      ..add(_conn.onStatus((status) {
        for (final listener in List.of(_listeners)) {
          try {
            listener.onConnectionStateChanged(status);
          } catch (_) {
            // A throwing listener must not starve the others.
          }
        }
      }))
      ..add(_conn.onReconnecting((attempt, delayMs) {
        for (final listener in List.of(_listeners)) {
          try {
            listener.onReconnecting(attempt, delayMs);
          } catch (_) {
            // Same isolation rule.
          }
        }
      }))
      ..add(_conn.onReconnected(() {
        for (final listener in List.of(_listeners)) {
          try {
            listener.onReconnected();
          } catch (_) {
            // Same isolation rule.
          }
        }
      }));
  }

  void stop() {
    for (final unsub in _unsubs) {
      unsub();
    }
    _unsubs.clear();
  }

  /// LOGIN with token sourcing: explicit token wins, then
  /// provider.getToken(), then userId itself (scaffold mode). AUTH_FAILED
  /// gives the provider one renewal chance (renewToken → a second LOGIN
  /// round); at most one renewal per login chain. The terminal outcome fans
  /// out onLoginResult + onAuthResult. Returns the final code (0 = logged in).
  Future<ErrorCode> login(String userId, {String? token}) async {
    var code = await _loginRound(token ?? _provider?.getToken() ?? userId);
    if (code == ErrorCode.AUTH_FAILED && _provider != null) {
      final fresh = await _provider!.renewToken();
      if (fresh != null) {
        code = await _loginRound(fresh);
      }
    }
    _terminalAuth(code, userId);
    return code;
  }

  /// Full-pipeline send. Throws RequestError(RequestErrorKind.closed) when
  /// not connected, (…blocked) for messages consumed before the wire, and
  /// ArgumentError for invalid arguments (C# ArgumentException parity).
  Future<chat.SendMessageResponse> send(
      SendOptions options, String content) async {
    if (_conn.status != ConnStatus.connected) {
      // 状态检查先于参数校验(C++ 参考实现顺序)。
      throw RequestError(RequestErrorKind.closed);
    }
    if (content.isEmpty) {
      throw RangeError('content is empty');
    }
    if (options.channelType == chat.ChannelType.PRIVATE) {
      if (options.receiverId == null || options.receiverId!.isEmpty) {
        throw ArgumentError.value(
            options.receiverId, 'receiverId', 'private send needs receiverId');
      }
    } else if (options.channelId == null || options.channelId!.isEmpty) {
      throw ArgumentError.value(options.channelId, 'channelId',
          '${options.channelType} send needs an explicit channelId');
    }

    if (content.startsWith('/')) {
      final routed = _routeCommand(content, _selfId());
      if (routed != null) {
        throw RequestError(RequestErrorKind.blocked, message: routed);
      }
    }

    final request = _buildSendRequest(options, content);
    final interceptor = _interceptor;
    if (interceptor != null) {
      var allowed = true;
      try {
        allowed = interceptor.onBeforeSend(request);
      } catch (_) {
        allowed = false; // a throwing interceptor is a blocking one
      }
      if (!allowed) {
        throw RequestError(RequestErrorKind.blocked,
            message: 'message blocked by interceptor');
      }
    }

    final store = _store;
    if (store != null) {
      try {
        store.save(_storedCopyOf(request));
      } catch (_) {
        // Archive failures must not block the send.
      }
    }

    final resp = await _conn.request<chat.SendMessageResponse>(
        specs.sendMessage, request);
    try {
      interceptor?.onAfterSend(request);
    } catch (_) {
      // Audit hooks must not break the caller.
    }
    return resp;
  }

  // ---- archive forwards (C++ LoadHistory/MarkRead/GetUnreadCount/Cleanup) --

  /// Newest-first slice; empty when no store is installed.
  List<ChatMessage> loadHistory(
    chat.ChannelType channelType,
    String channelId,
    int limit, {
    int? beforeTimestamp,
  }) =>
      _store?.load(channelType, channelId, limit,
          beforeTimestamp: beforeTimestamp) ??
      const <ChatMessage>[];

  void markRead(
          chat.ChannelType channelType, String channelId, String messageId) =>
      _store?.markRead(channelType, channelId, messageId);

  int unreadCount(chat.ChannelType channelType, String channelId) =>
      _store?.getUnreadCount(channelType, channelId) ?? 0;

  void cleanup(int olderThanMs) => _store?.cleanup(olderThanMs);

  // ---- internals -----------------------------------------------------------

  /// null = pass through (no handlers); a string = the blocked reason.
  String? _routeCommand(String content, String senderId) {
    if (_commands.isEmpty) return null;
    var name = content.substring(1);
    var args = '';
    final space = name.indexOf(' ');
    if (space >= 0) {
      args = name.substring(space + 1);
      name = name.substring(0, space);
    }
    var handled = false;
    for (final command in _commands) {
      if (command.name != name) continue;
      try {
        handled = command.execute(args, senderId);
      } catch (_) {
        handled = false; // a throwing handler declines, like C++/C#
      }
      if (handled) break;
    }
    return handled
        ? 'command handled locally'
        : 'unknown command, dropped locally: $content';
  }

  chat.SendMessageRequest _buildSendRequest(
      SendOptions options, String content) {
    final senderId = _selfId();
    var channelId = options.channelId ?? '';
    if (options.channelType == chat.ChannelType.PRIVATE) {
      // 双方 id 字典序小者在前,与服务端/c++/c#/web 同款。
      channelId = ([senderId, options.receiverId!]..sort()).join('|');
    }
    return chat.SendMessageRequest(
      senderId: senderId,
      receiverId: options.receiverId ?? '',
      channelType: options.channelType,
      channelId: channelId,
      msgType: options.msgType ?? chat.MsgType.TEXT,
      content: Uint8List.fromList(utf8.encode(content)),
      clientTimestamp: Int64(DateTime.now().millisecondsSinceEpoch),
      replyToMessageId: options.replyToMessageId ?? '',
    );
  }

  ChatMessage _storedCopyOf(chat.SendMessageRequest request) => ChatMessage(
        senderId: request.senderId,
        receiverId: request.receiverId,
        channelType: request.channelType,
        channelId: request.channelId,
        msgType: request.msgType,
        content: Uint8List.fromList(request.content),
        timestamp: request.clientTimestamp,
        replyToMessageId: request.replyToMessageId,
      );

  /// Interceptor → archive → listeners → onAfterReceive; drop kills all.
  void _onIncoming(Uint8List body) {
    ChatMessage message;
    try {
      message = ChatMessage.fromBuffer(body);
    } catch (_) {
      return; // undecodable: other notify subscribers still got the raw body
    }
    final interceptor = _interceptor;
    if (interceptor != null) {
      var allowed = true;
      try {
        allowed = interceptor.onBeforeReceive(message);
      } catch (_) {
        allowed = false; // a throwing interceptor is a blocking one
      }
      if (!allowed) return;
    }
    final store = _store;
    if (store != null) {
      try {
        store.save(message);
      } catch (_) {
        // Archive failures must not kill the fan-out.
      }
    }
    for (final listener in List.of(_listeners)) {
      try {
        listener.onMessageReceived(message);
      } catch (_) {
        // A throwing listener must not starve the others.
      }
    }
    try {
      interceptor?.onAfterReceive(message);
    } catch (_) {
      // Same isolation rule.
    }
  }

  void _onKickBody(Uint8List body) {
    var reason = '';
    try {
      reason = auth.KickNotify.fromBuffer(body).reason;
    } catch (_) {
      // A malformed kick still proves the session is dead.
    }
    for (final listener in List.of(_listeners)) {
      try {
        listener.onKicked(reason);
      } catch (_) {
        // Same isolation rule.
      }
    }
  }

  Future<ErrorCode> _loginRound(String token) async {
    final resp = await _conn.request<auth.LoginResponse>(
      specs.login,
      auth.LoginRequest(
        token: token,
        deviceId: _deviceId(),
        platform: 'android',
      ),
    );
    if (resp.code == ErrorCode.OK) {
      _conn.resetBackoff();
    }
    return resp.code;
  }

  void _terminalAuth(ErrorCode code, String userId) {
    for (final listener in List.of(_listeners)) {
      try {
        listener.onLoginResult(code, userId);
      } catch (_) {
        // Same isolation rule.
      }
    }
    try {
      _provider?.onAuthResult(code, userId);
    } catch (_) {
      // Same isolation rule.
    }
  }
}
