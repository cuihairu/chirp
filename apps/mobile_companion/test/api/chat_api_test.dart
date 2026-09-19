import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/api/chat_api.dart';
import 'package:chirp_mobile/protocol/chat_connection.dart';
import 'package:chirp_mobile/protocol/chirp_client.dart';
import 'package:chirp_mobile/protocol/errors.dart';
import 'package:chirp_mobile/protocol/msg_map.dart';
import 'package:chirp_mobile/state/auth_store.dart';
import 'package:chirp_mobile/state/conversation_store.dart';
import 'package:chirp_mobile/state/message_store.dart';
import 'package:chirp_mobile/state/models.dart';
import 'package:chirp_mobile/state/typing_presence.dart';
import 'package:protobuf/protobuf.dart';

/// Scripted ChatConnection: canned responses per request msgId, recorded
/// requests/sends, manually dispatched notifies.
class FakeConnection implements ChatConnection {
  FakeConnection();

  /// msgId → response body factory; missing entries throw.
  final Map<MsgID, GeneratedMessage Function(GeneratedMessage)> responses = {};
  final List<(MsgID, GeneratedMessage)> requests = [];
  final List<(MsgID, Uint8List)> sends = [];
  final Map<MsgID, void Function(Uint8List)> notifyHandlers = {};
  int connectCount = 0;
  int disconnectCount = 0;

  @override
  Future<void> connect() async {
    connectCount++;
  }

  @override
  void disconnect() {
    disconnectCount++;
  }

  @override
  void resetBackoff() {}

  @override
  Future<TResp> request<TResp extends GeneratedMessage>(
    MessageSpec<TResp> spec,
    GeneratedMessage request, {
    int? timeoutMs,
  }) async {
    requests.add((spec.reqMsgId, request));
    final factory = responses[spec.reqMsgId];
    if (factory == null) {
      throw StateError('no scripted response for ${spec.reqMsgId.name}');
    }
    return spec.decodeResponse(factory(request).writeToBuffer());
  }

  @override
  void send(MsgID msgId, Uint8List body) => sends.add((msgId, body));

  @override
  void Function() onNotify(MsgID msgId, void Function(Uint8List) handler) {
    notifyHandlers[msgId] = handler;
    return () => notifyHandlers.remove(msgId);
  }

  final listeners = <void Function(ConnStatus)>[];

  @override
  void onStatus(void Function(ConnStatus) listener) => listeners.add(listener);

  @override
  void heartbeatNow() {}

  @override
  ConnStatus get status => ConnStatus.connected;

  @override
  bool get kicked => false;

  void dispatch(MsgID msgId, GeneratedMessage notify) {
    notifyHandlers[msgId]!(notify.writeToBuffer());
  }
}

class Harness {
  Harness() {
    api = ChatApi(
      conn: conn,
      auth: auth,
      conversations: conversations,
      messages: messages,
      typing: typing,
    );
  }

  final conn = FakeConnection();
  final auth = createAuthStore(() => 'device-1');
  final conversations = createConversationStore();
  final messages = createMessageStore();
  final typing = createTypingStore();
  late final ChatApi api;
}

ChannelRef privateChannel() => const ChannelRef(
      key: 'p:a|b',
      kind: ConversationKind.private,
      channelId: 'a|b',
      peerId: 'b',
    );

void main() {
  test('login success flips auth and registers notify handlers', () async {
    final h = Harness();
    h.conn.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.OK, userId: 'a');
    final code = await h.api.login('a');
    expect(code, ErrorCode.OK);
    expect(h.auth.value.loggedIn, isTrue);
    expect(h.auth.value.userId, 'a');
    expect(h.notifyHandlersUp, isTrue);
    expect(
      h.requestsLogin.deviceId,
      'device-1',
    );
  });

  test('login failure throws a server RequestError, auth untouched', () async {
    final h = Harness();
    h.conn.responses[MsgID.LOGIN_REQ] =
        (_) => LoginResponse(code: ErrorCode.AUTH_FAILED);
    await expectLater(
      h.api.login('a'),
      throwsA(isA<RequestError>()
          .having((e) => e.code, 'code', ErrorCode.AUTH_FAILED)),
    );
    expect(h.auth.value.loggedIn, isFalse);
  });

  test('send confirms the optimistic message; offline queues it', () async {
    final h = Harness();
    h.auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
    h.api.setActiveChannel('p:a|b'); // suppress unread bumping
    h.conn.responses[MsgID.SEND_MESSAGE_REQ] = (request) =>
        SendMessageResponse(code: ErrorCode.OK, messageId: 'srv-1');

    await h.api.sendToChannel(privateChannel(), 'hello');
    var list = messagesOf(h.messages.value, 'p:a|b');
    expect(list, hasLength(1));
    expect(list.single.messageId, 'srv-1');
    expect(list.single.pending, isFalse);
    expect(list.single.failed, isFalse);

    h.conn.responses[MsgID.SEND_MESSAGE_REQ] = (request) =>
        SendMessageResponse(code: ErrorCode.TARGET_OFFLINE, messageId: 'srv-2');
    await h.api.sendToChannel(privateChannel(), 'again');
    list = messagesOf(h.messages.value, 'p:a|b');
    expect(list, hasLength(2));
    expect(list.last.messageId, 'srv-2');
    expect(list.last.queuedOffline, isTrue);
  });

  test('server-rejected send marks the message failed', () async {
    final h = Harness();
    h.auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
    h.conn.responses[MsgID.SEND_MESSAGE_REQ] =
        (request) => SendMessageResponse(code: ErrorCode.RATE_LIMITED);
    await h.api.sendToChannel(privateChannel(), 'spam');
    final list = messagesOf(h.messages.value, 'p:a|b');
    expect(list.single.failed, isTrue);
  });

  test(
      'incoming message appends, unread bumps outside active channel, '
      'ack fires, listeners fire', () async {
    final h = Harness();
    h.auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
    h.api.start();

    final incoming = <IncomingMessage>[];
    h.api.onMessage(incoming.add);

    h.conn.dispatch(
      MsgID.CHAT_MESSAGE_NOTIFY,
      ChatMessage(
        messageId: 'm1',
        senderId: 'b',
        channelType: ChannelType.PRIVATE,
        channelId: 'a|b',
        content: Uint8List.fromList('hi there'.codeUnits),
        timestamp: Int64(1234),
      ),
    );

    final list = messagesOf(h.messages.value, 'p:a|b');
    expect(list.single.content, 'hi there');
    expect(incoming.single.content, 'hi there');
    expect(h.conversations.value.conversations.single.key, 'p:a|b');
    expect(h.conversations.value.conversations.single.unreadLocal, 1);
    // supportsMessageAck=true: every live push is acked.
    expect(h.conn.sends.single.$1, MsgID.MESSAGE_ACK);
    expect(ChatAckProbe.decode(h.conn.sends.single.$2).messageId, 'm1');

    // Same message inside the active channel must not bump unread.
    h.api.setActiveChannel('p:a|b');
    h.conn.dispatch(
      MsgID.CHAT_MESSAGE_NOTIFY,
      ChatMessage(
        messageId: 'm2',
        senderId: 'b',
        channelType: ChannelType.PRIVATE,
        channelId: 'a|b',
        content: Uint8List.fromList('again'.codeUnits),
        timestamp: Int64(1235),
      ),
    );
    expect(
      h.conversations.value.conversations
          .firstWhere((c) => c.key == 'p:a|b')
          .unreadLocal,
      1,
    );
  });

  test('typing notify updates the typing store', () async {
    final h = Harness();
    h.auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
    h.api.start();
    h.conn.dispatch(
      MsgID.TYPING_INDICATOR_NOTIFY,
      TypingIndicator(
        userId: 'b',
        channelType: ChannelType.PRIVATE,
        channelId: 'a|b',
        isTyping: true,
      ),
    );
    expect(
        typingUsersOf(
            h.typing.value, 'p:a|b', DateTime.now().millisecondsSinceEpoch),
        ['b']);
  });

  test('read notify records the peer cursor, not our own echo', () async {
    final h = Harness();
    h.auth.update((prev) => prev.copyWith(userId: 'a', loggedIn: true));
    h.api.start();
    h.conn.dispatch(
      MsgID.MESSAGE_READ_NOTIFY,
      MessageReadNotify(
        readerUserId: 'b',
        channelType: ChannelType.PRIVATE,
        channelId: 'a|b',
        messageId: 'm1',
      ),
    );
    expect(readCursorOf(h.messages.value, 'p:a|b', 'b'), 'm1');

    h.conn.dispatch(
      MsgID.MESSAGE_READ_NOTIFY,
      MessageReadNotify(
        readerUserId: 'a', // own echo
        channelType: ChannelType.PRIVATE,
        channelId: 'a|b',
        messageId: 'm2',
      ),
    );
    expect(readCursorOf(h.messages.value, 'p:a|b', 'a'), isNull);
  });
}

extension on Harness {
  bool get notifyHandlersUp =>
      conn.notifyHandlers.containsKey(MsgID.CHAT_MESSAGE_NOTIFY);

  LoginRequest get requestsLogin =>
      conn.requests.where((r) => r.$1 == MsgID.LOGIN_REQ).map((r) => r.$2).first
          as LoginRequest;
}

/// Tiny probe so the test does not import the protobuf of MESSAGE_ACK twice.
class ChatAckProbe {
  static MessageAck decode(Uint8List body) => MessageAck.fromBuffer(body);
}
