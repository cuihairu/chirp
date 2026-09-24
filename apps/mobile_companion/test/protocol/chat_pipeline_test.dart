import 'dart:convert';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_proto/proto/auth.pb.dart' as auth;
import 'package:chirp_proto/proto/chat.pb.dart' as chat;
import 'package:fixnum/fixnum.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:protobuf/protobuf.dart';

import 'package:chirp_mobile/protocol/chirp_client.dart';
import 'package:chirp_mobile/protocol/chat_pipeline.dart';
import 'package:chirp_mobile/protocol/errors.dart';
import 'package:chirp_mobile/protocol/hooks.dart';
import 'package:chirp_mobile/protocol/word_filter.dart';

import 'fake_transport.dart';

/// Pipeline on a real ChirpClient + FakeTransport: send/notify assertions
/// read the wire through [FakeTransport.lastSentPacket]. Every transport
/// the test creates (initial + reconnects) lands in [transports].
class Harness {
  Harness._({ChirpClientOptions options = const ChirpClientOptions()}) {
    client = ChirpClient(
      url: 'ws://test',
      transportFactory: (_) {
        final t = FakeTransport();
        transports.add(t);
        return t;
      },
      options: options,
    );
    pipeline = ChatPipeline(
      conn: client,
      selfId: () => 'u1',
      deviceId: () => 'dev-mobile',
    );
    pipeline.start();
  }

  static Future<Harness> create({
    ChirpClientOptions options = const ChirpClientOptions(),
  }) async {
    final h = Harness._(options: options);
    await h.client.connect();
    return h;
  }

  final transports = <FakeTransport>[];
  late final ChirpClient client;
  late final ChatPipeline pipeline;

  /// The transport the client is currently on (last after a reconnect).
  FakeTransport get wire => transports.last;

  /// Logs in synchronously against the current transport.
  Future<void> loginOk() async {
    final pending = pipeline.login('u1');
    wire.serverPacket(Packet(
      msgId: MsgID.LOGIN_RESP,
      sequence: wire.lastSentPacket().sequence,
      body:
          auth.LoginResponse(code: ErrorCode.OK, userId: 'u1').writeToBuffer(),
    ));
    expect(await pending, ErrorCode.OK);
  }

  /// Answers whatever request went on the wire last.
  void respondLast(MsgID respId, GeneratedMessage body) {
    wire.serverPacket(Packet(
      msgId: respId,
      sequence: wire.lastSentPacket().sequence,
      body: body.writeToBuffer(),
    ));
  }
}

chat.ChatMessage chatOf(
        String id, String senderId, String content, int timestamp) =>
    chat.ChatMessage(
      messageId: id,
      senderId: senderId,
      channelType: chat.ChannelType.PRIVATE,
      channelId: '$senderId|u1',
      content: Uint8List.fromList(utf8.encode(content)),
      timestamp: Int64(timestamp),
    );

void main() {
  group('send pipeline', () {
    test('sends to the wire with normalized private channel and reply id',
        () async {
      final h = await Harness.create()
        ..pipeline.store = MemoryMessageStore();
      await h.loginOk();

      final sending = h.pipeline.send(
        const SendOptions(
          channelType: chat.ChannelType.PRIVATE,
          receiverId: 'peer9',
          replyToMessageId: 'm-42',
        ),
        'gg',
      );
      final packet = h.wire.lastSentPacket();
      expect(packet.msgId, MsgID.SEND_MESSAGE_REQ);
      final req = chat.SendMessageRequest.fromBuffer(packet.body);
      expect(req.senderId, 'u1');
      expect(req.receiverId, 'peer9');
      expect(req.channelType, chat.ChannelType.PRIVATE);
      expect(req.channelId, 'peer9|u1'); // sorted pair, server parity
      expect(req.msgType, chat.MsgType.TEXT);
      expect(req.replyToMessageId, 'm-42');
      expect(utf8.decode(req.content), 'gg');

      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK, messageId: 'srv-1'));
      final resp = await sending;
      expect(resp.code, ErrorCode.OK);
      expect(resp.messageId, 'srv-1');
    });

    test(
        'sends non-private traffic with the explicit channelId and no receiver',
        () async {
      final h = await Harness.create();
      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.TEAM, channelId: 'team-7'),
          'hi');
      final req =
          chat.SendMessageRequest.fromBuffer(h.wire.lastSentPacket().body);
      expect(req.channelType, chat.ChannelType.TEAM);
      expect(req.channelId, 'team-7'); // passed through, not derived
      expect(req.receiverId, '');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;
    });

    test('validates in the C++ order: connection first, then arguments',
        () async {
      final h = await Harness.create();
      // Argument checks fire while connected (C# ArgumentException parity).
      await expectLater(
        h.pipeline.send(
            const SendOptions(channelType: chat.ChannelType.PRIVATE), 'x'),
        throwsArgumentError,
      );
      await expectLater(
        h.pipeline
            .send(const SendOptions(channelType: chat.ChannelType.TEAM), 'x'),
        throwsArgumentError,
      );
      await expectLater(
        h.pipeline.send(
            const SendOptions(
                channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
            ''),
        throwsRangeError,
      );

      h.client.disconnect(); // …but the status check wins over all of them
      await expectLater(
        h.pipeline.send(
            const SendOptions(
                channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
            ''),
        throwsA(isA<RequestError>()
            .having((e) => e.kind, 'kind', RequestErrorKind.closed)),
      );
    });

    test('passes /-text through when no handlers are registered', () async {
      final h = await Harness.create();
      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
          '/dance');
      expect(h.wire.lastSentPacket().msgId, MsgID.SEND_MESSAGE_REQ);
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;
    });

    test('consumes /-commands locally: hit, miss and handler throw', () async {
      final h = await Harness.create();
      var throwerRan = false;
      final offThrow = h.pipeline.registerCommand(_FnCommand('trade', (args) {
        throwerRan = true;
        throw StateError('handler bug');
      }));
      final offChain = h.pipeline.registerCommand(_FnCommand('trade', (args) {
        expect(args, 'alice 100');
        return true;
      }));
      for (final text in ['/trade alice 100', '/nothing here', '/trade']) {
        final before = h.wire.sent.length;
        await expectLater(
          h.pipeline.send(
              const SendOptions(
                  channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
              text),
          throwsA(isA<RequestError>()
              .having((e) => e.kind, 'kind', RequestErrorKind.blocked)),
        );
        expect(h.wire.sent.length, before);
      }
      expect(
          throwerRan, isTrue); // the throwing handler declined, next got a try
      offThrow();
      offChain();
    });

    test('interceptor rewrites reach the wire; drops and throws never do',
        () async {
      final h = await Harness.create();
      h.pipeline.interceptor = _FnInterceptor(
        onBeforeSend: (request) {
          request.content = Uint8List.fromList(utf8.encode('rewritten'));
          return true;
        },
      );
      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
          'raw');
      final req =
          chat.SendMessageRequest.fromBuffer(h.wire.lastSentPacket().body);
      expect(utf8.decode(req.content), 'rewritten');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;

      h.pipeline.interceptor = _FnInterceptor(onBeforeSend: (_) => false);
      final before = h.wire.sent.length;
      await expectLater(
        h.pipeline.send(
            const SendOptions(
                channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
            'x'),
        throwsA(isA<RequestError>()
            .having((e) => e.kind, 'kind', RequestErrorKind.blocked)
            .having(
                (e) => e.message, 'message', 'message blocked by interceptor')),
      );
      h.pipeline.interceptor = _FnInterceptor(
        onBeforeSend: (_) => throw StateError('hook bug'),
      );
      await expectLater(
        h.pipeline.send(
            const SendOptions(
                channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
            'x'),
        throwsA(isA<RequestError>()
            .having((e) => e.kind, 'kind', RequestErrorKind.blocked)),
      );
      expect(h.wire.sent.length, before);
    });

    test('archives both directions newest-first; unread is 0 without tracking',
        () async {
      final h = await Harness.create()
        ..pipeline.store = MemoryMessageStore();
      await h.loginOk();

      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p9'),
          'out');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;

      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('in-1', 'p9', 'in', DateTime.now().millisecondsSinceEpoch)
            .writeToBuffer(),
      ));
      await h.wire.settle(); // the notify fan-out runs on the microtask queue

      final history =
          h.pipeline.loadHistory(chat.ChannelType.PRIVATE, 'p9|u1', 10);
      expect(history.map((m) => utf8.decode(m.content)), ['in', 'out']);
      expect(h.pipeline.unreadCount(chat.ChannelType.PRIVATE, 'p9|u1'), 0);
    });

    test('word filter rewrites before the wire and the archive; reject drops',
        () async {
      final h = await Harness.create()
        ..pipeline.store = MemoryMessageStore()
        ..pipeline.interceptor = WordFilterInterceptor(
            WordFilterOptions(terms: parseWordLexicon(['damn'])));
      await h.loginOk();

      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p9'),
          'well damn, hi');
      final req =
          chat.SendMessageRequest.fromBuffer(h.wire.lastSentPacket().body);
      expect(utf8.decode(req.content), 'well **, hi');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;

      final history =
          h.pipeline.loadHistory(chat.ChannelType.PRIVATE, 'p9|u1', 10);
      expect(utf8.decode(history.last.content), 'well **, hi');

      // reject 命中 = 本地拦截,零发包。
      h.pipeline.interceptor = WordFilterInterceptor(WordFilterOptions(
          terms: ['banned'], policy: WordFilterPolicy.reject));
      final before = h.wire.sent.length;
      await expectLater(
        h.pipeline.send(
            const SendOptions(
                channelType: chat.ChannelType.PRIVATE, receiverId: 'p9'),
            'banned goods'),
        throwsA(isA<RequestError>()
            .having((e) => e.kind, 'kind', RequestErrorKind.blocked)),
      );
      expect(h.wire.sent.length, before);
    });

    test(
        'receive pipeline: listeners get the message; drop kills the fan-out only',
        () async {
      final h = await Harness.create();
      final seen = <String>[];
      final rawCount = <int>[];
      final offRaw =
          h.client.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, (_) => rawCount.add(1));
      final off = h.pipeline.addListener(_FnListener(
        onMessageReceived: (m) => seen.add(utf8.decode(m.content)),
      ));

      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m1', 'p9', 'hello', 5).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['hello']);
      expect(rawCount, [1]); // raw notify subscribers unaffected

      h.pipeline.interceptor = _FnInterceptor(onBeforeReceive: (_) => false);
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m2', 'p9', 'dropped', 6).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['hello']); // listener never saw the drop
      expect(rawCount, [1, 1]); // …but raw subscribers still did

      off();
      offRaw();
    });

    test('receive interceptor: pass-through keeps the fan-out; throw blocks it',
        () async {
      final h = await Harness.create();
      final seen = <String>[];
      final off = h.pipeline.addListener(_FnListener(
        onMessageReceived: (m) => seen.add(utf8.decode(m.content)),
      ));

      h.pipeline.interceptor = _FnInterceptor(onBeforeReceive: (_) => true);
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m1', 'p9', 'kept', 1).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['kept']);

      h.pipeline.interceptor = _FnInterceptor(
        onBeforeReceive: (_) => throw StateError('hook bug'),
      );
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m2', 'p9', 'dropped', 2).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['kept']); // throwing interceptor = blocking one
      off();
    });

    test('after-hooks fire around the wire in order', () async {
      final h = await Harness.create();
      final trail = <String>[];
      h.pipeline.interceptor = _FnInterceptor(
        onAfterSend: (_) => trail.add('afterSend'),
        onAfterReceive: (_) => trail.add('afterReceive'),
      );
      final off = h.pipeline.addListener(
          _FnListener(onMessageReceived: (_) => trail.add('listener')));

      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
          'x');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m1', 'p9', 'in', 1).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(trail, ['afterSend', 'listener', 'afterReceive']);
      off();
    });

    test('a failing store never blocks send or receive', () async {
      final h = await Harness.create()
        ..pipeline.store = _ThrowingStore();
      final seen = <String>[];
      final off = h.pipeline.addListener(_FnListener(
          onMessageReceived: (m) => seen.add(utf8.decode(m.content))));

      final sending = h.pipeline.send(
          const SendOptions(
              channelType: chat.ChannelType.PRIVATE, receiverId: 'p'),
          'x');
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m1', 'p9', 'kept', 1).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['kept']);
      off();
    });

    test('allows a custom msgType on send', () async {
      final h = await Harness.create();
      final sending = h.pipeline.send(
        const SendOptions(
          channelType: chat.ChannelType.PRIVATE,
          receiverId: 'p',
          msgType: chat.MsgType.EMOJI,
        ),
        ':)',
      );
      final req =
          chat.SendMessageRequest.fromBuffer(h.wire.lastSentPacket().body);
      expect(req.msgType, chat.MsgType.EMOJI);
      h.respondLast(MsgID.SEND_MESSAGE_RESP,
          chat.SendMessageResponse(code: ErrorCode.OK));
      await sending;
    });
  });

  group('login orchestration', () {
    test('prefers the explicit token, then falls back to the provider',
        () async {
      final h = await Harness.create()
        ..pipeline.provider = _FnProvider(getToken: () => 'from-provider');

      final p1 = h.pipeline.login('u1', token: 'explicit-token');
      expect(auth.LoginRequest.fromBuffer(h.wire.lastSentPacket().body).token,
          'explicit-token');
      h.respondLast(MsgID.LOGIN_RESP,
          auth.LoginResponse(code: ErrorCode.OK, userId: 'u1'));
      await p1;

      final p2 = h.pipeline.login('u1');
      expect(auth.LoginRequest.fromBuffer(h.wire.lastSentPacket().body).token,
          'from-provider');
      h.respondLast(MsgID.LOGIN_RESP,
          auth.LoginResponse(code: ErrorCode.OK, userId: 'u1'));
      await p2;
    });

    test('renews exactly once on AUTH_FAILED and fans out the terminal result',
        () async {
      final h = await Harness.create();
      final authResults = <ErrorCode>[];
      final loginResults = <ErrorCode>[];
      h.pipeline.provider = _FnProvider(
        getToken: () => 'stale',
        renewToken: () async => 'fresh',
        onAuthResult: (code, _) => authResults.add(code),
      );
      final off = h.pipeline.addListener(
          _FnListener(onLoginResult: (code, _) => loginResults.add(code)));

      final pending = h.pipeline.login('u1');
      expect(auth.LoginRequest.fromBuffer(h.wire.lastSentPacket().body).token,
          'stale');
      h.respondLast(
          MsgID.LOGIN_RESP, auth.LoginResponse(code: ErrorCode.AUTH_FAILED));
      await h.wire.settle();
      expect(auth.LoginRequest.fromBuffer(h.wire.lastSentPacket().body).token,
          'fresh');
      h.respondLast(MsgID.LOGIN_RESP,
          auth.LoginResponse(code: ErrorCode.OK, userId: 'u1'));
      expect(await pending, ErrorCode.OK);
      expect(authResults, [ErrorCode.OK]);
      expect(loginResults, [ErrorCode.OK]);
      off();
    });

    test('renewal failing again ends the login at AUTH_FAILED, once', () async {
      final h = await Harness.create();
      h.pipeline.provider = _FnProvider(
        getToken: () => 'stale',
        renewToken: () async => 'also-stale',
      );
      final pending = h.pipeline.login('u1');
      h.respondLast(
          MsgID.LOGIN_RESP, auth.LoginResponse(code: ErrorCode.AUTH_FAILED));
      await h.wire.settle();
      h.respondLast(
          MsgID.LOGIN_RESP, auth.LoginResponse(code: ErrorCode.AUTH_FAILED));
      expect(await pending, ErrorCode.AUTH_FAILED);
      expect(h.wire.sent, hasLength(2)); // two LOGINs, no third
    });

    test('a declining provider ends the login immediately', () async {
      final h = await Harness.create();
      h.pipeline.provider = _FnProvider(
        getToken: () => 'stale',
        renewToken: () async => null,
      );
      final pending = h.pipeline.login('u1');
      h.respondLast(
          MsgID.LOGIN_RESP, auth.LoginResponse(code: ErrorCode.AUTH_FAILED));
      expect(await pending, ErrorCode.AUTH_FAILED);
      expect(h.wire.sent, hasLength(1)); // no second round without a token
    });
  });

  group('listener wiring', () {
    test('sees reconnecting/reconnected and the kick reason', () async {
      final h = await Harness.create(
        options: const ChirpClientOptions(
          reconnectBaseMs: 5,
          reconnectMaxMs: 10,
        ),
      );
      final events = <String>[];
      var lastDelay = 0;
      var kickReason = '';
      final off = h.pipeline.addListener(_FnListener(
        onReconnecting: (attempt, delayMs) {
          events.add('reconnecting-$attempt');
          lastDelay = delayMs;
        },
        onReconnected: () => events.add('reconnected'),
        onKicked: (reason) {
          kickReason = reason;
          events.add('kicked');
        },
      ));

      h.wire.close(); // transport drop → close event (async on the stream)
      await h.wire.settle();
      expect(events, ['reconnecting-1']);
      expect(lastDelay, greaterThan(0));

      // The 5-10ms backoff timer fires a reconnect onto a second transport.
      // Poll instead of sleeping: a loaded CI box stretches real timers, and
      // a fixed wait would flake the whole suite.
      for (var i = 0; h.transports.length < 2 && i < 100; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
      }
      await h.wire.settle();
      expect(h.transports, hasLength(2));
      expect(events, ['reconnecting-1', 'reconnected']);

      h.transports.last.serverPacket(Packet(
        msgId: MsgID.KICK_NOTIFY,
        body: auth.KickNotify(reason: 'top-up elsewhere').writeToBuffer(),
      ));
      await h.transports.last.settle();
      expect(kickReason, 'top-up elsewhere');
      off();
    });

    test('start() is idempotent and stop()/start() rewires cleanly', () async {
      final h = await Harness.create();
      final seen = <String>[];
      final off = h.pipeline.addListener(_FnListener(
        onMessageReceived: (m) => seen.add(utf8.decode(m.content)),
      ));
      h.pipeline.start(); // second call must not double-subscribe

      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m1', 'p9', 'once', 1).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['once']);

      h.pipeline.stop();
      h.pipeline.start();
      h.wire.serverPacket(Packet(
        msgId: MsgID.CHAT_MESSAGE_NOTIFY,
        body: chatOf('m2', 'p9', 'again', 2).writeToBuffer(),
      ));
      await h.wire.settle();
      expect(seen, ['once', 'again']);
      off();
    });

    test('works without a store: queries degrade to empties and no-ops',
        () async {
      final h = await Harness.create();
      expect(h.pipeline.loadHistory(chat.ChannelType.PRIVATE, 'p9|u1', 10),
          isEmpty);
      expect(h.pipeline.unreadCount(chat.ChannelType.PRIVATE, 'p9|u1'), 0);
      h.pipeline.markRead(chat.ChannelType.PRIVATE, 'p9|u1', 'm1'); // no-op
      h.pipeline.cleanup(0); // no-op
    });

    test('markRead and cleanup forward to a store that implements them',
        () async {
      final h = await Harness.create();
      final reads = <String>[];
      final cleanups = <int>[];
      h.pipeline.store =
          _RecordingStore(reads: reads.add, cleanups: cleanups.add);
      h.pipeline.markRead(chat.ChannelType.PRIVATE, 'p9|u1', 'm1');
      h.pipeline.cleanup(1234);
      expect(reads, ['m1']);
      expect(cleanups, [1234]);
    });

    test('status flips fan out to listeners', () async {
      final h = await Harness.create();
      final statuses = <ConnStatus>[];
      final off = h.pipeline
          .addListener(_FnListener(onConnectionStateChanged: statuses.add));
      h.client.disconnect();
      expect(statuses.last, ConnStatus.closed);
      off();
    });
  });

  group('MemoryMessageStore', () {
    chat.ChatMessage msgOf(String id, int timestamp) => chat.ChatMessage(
          messageId: id,
          channelType: chat.ChannelType.PRIVATE,
          channelId: 'a|b',
          content: Uint8List.fromList(utf8.encode(id)),
          timestamp: Int64(timestamp),
        );

    test('evicts oldest beyond the cap and loads newest-first', () {
      final s = MemoryMessageStore(maxPerChannel: 3);
      for (var i = 1; i <= 4; i++) {
        s.save(msgOf('m$i', i));
      }
      expect(
        s.load(chat.ChannelType.PRIVATE, 'a|b', 10).map((m) => m.messageId),
        ['m4', 'm3', 'm2'],
      );
    });

    test('respects limit and rejects a non-positive one', () {
      final s = MemoryMessageStore();
      for (var i = 1; i <= 3; i++) {
        s.save(msgOf('m$i', i));
      }
      expect(
        s.load(chat.ChannelType.PRIVATE, 'a|b', 2).map((m) => m.messageId),
        ['m3', 'm2'],
      );
      expect(s.load(chat.ChannelType.PRIVATE, 'a|b', 0), isEmpty);
      expect(s.load(chat.ChannelType.PRIVATE, 'missing', 5), isEmpty);
    });

    test('applies beforeTimestamp as an exclusive upper bound', () {
      final s = MemoryMessageStore();
      for (var i = 1; i <= 3; i++) {
        s.save(msgOf('m$i', i));
      }
      expect(s.load(chat.ChannelType.PRIVATE, 'a|b', 10, beforeTimestamp: 0),
          hasLength(3)); // 0 = no bound
      expect(
        s
            .load(chat.ChannelType.PRIVATE, 'a|b', 10, beforeTimestamp: 3)
            .map((m) => m.messageId),
        ['m2', 'm1'],
      );
    });

    test('cleanup drops expired buckets and keeps partially fresh ones', () {
      final s = MemoryMessageStore();
      s.save(msgOf('old', 100));
      s.save(chat.ChatMessage(
        messageId: 'fresh',
        channelType: chat.ChannelType.WORLD,
        channelId: 'world',
        timestamp: Int64(500),
      ));
      s.cleanup(300); // private bucket empties (dropped), world keeps one
      expect(s.load(chat.ChannelType.PRIVATE, 'a|b', 10), isEmpty);
      expect(
        s.load(chat.ChannelType.WORLD, 'world', 10).map((m) => m.messageId),
        ['fresh'],
      );
    });
  });
}

/// Minimal ChatEventListener stand-in; only the overridden hooks fire.
class _FnListener extends ChatEventListener {
  _FnListener({
    void Function(ConnStatus)? onConnectionStateChanged,
    void Function(ErrorCode, String)? onLoginResult,
    void Function(String)? onKicked,
    void Function(int, int)? onReconnecting,
    void Function()? onReconnected,
    void Function(chat.ChatMessage)? onMessageReceived,
  })  : _onConnectionStateChanged = onConnectionStateChanged,
        _onLoginResult = onLoginResult,
        _onKicked = onKicked,
        _onReconnecting = onReconnecting,
        _onReconnected = onReconnected,
        _onMessageReceived = onMessageReceived;

  final void Function(ConnStatus)? _onConnectionStateChanged;
  final void Function(ErrorCode, String)? _onLoginResult;
  final void Function(String)? _onKicked;
  final void Function(int, int)? _onReconnecting;
  final void Function()? _onReconnected;
  final void Function(chat.ChatMessage)? _onMessageReceived;

  @override
  void onConnectionStateChanged(ConnStatus status) =>
      _onConnectionStateChanged?.call(status);

  @override
  void onLoginResult(ErrorCode code, String userId) =>
      _onLoginResult?.call(code, userId);

  @override
  void onKicked(String reason) => _onKicked?.call(reason);

  @override
  void onReconnecting(int attempt, int delayMs) =>
      _onReconnecting?.call(attempt, delayMs);

  @override
  void onReconnected() => _onReconnected?.call();

  @override
  void onMessageReceived(chat.ChatMessage message) =>
      _onMessageReceived?.call(message);
}

class _FnInterceptor extends MessageInterceptor {
  _FnInterceptor({
    bool Function(chat.SendMessageRequest)? onBeforeSend,
    bool Function(chat.ChatMessage)? onBeforeReceive,
    void Function(chat.SendMessageRequest)? onAfterSend,
    void Function(chat.ChatMessage)? onAfterReceive,
  })  : _onBeforeSend = onBeforeSend,
        _onBeforeReceive = onBeforeReceive,
        _onAfterSend = onAfterSend,
        _onAfterReceive = onAfterReceive;

  final bool Function(chat.SendMessageRequest)? _onBeforeSend;
  final bool Function(chat.ChatMessage)? _onBeforeReceive;
  final void Function(chat.SendMessageRequest)? _onAfterSend;
  final void Function(chat.ChatMessage)? _onAfterReceive;

  @override
  bool onBeforeSend(chat.SendMessageRequest request) =>
      _onBeforeSend?.call(request) ?? true;

  @override
  bool onBeforeReceive(chat.ChatMessage message) =>
      _onBeforeReceive?.call(message) ?? true;

  @override
  void onAfterSend(chat.SendMessageRequest request) =>
      _onAfterSend?.call(request);

  @override
  void onAfterReceive(chat.ChatMessage message) =>
      _onAfterReceive?.call(message);
}

class _FnProvider extends AuthProvider {
  _FnProvider({
    String Function()? getToken,
    Future<String?> Function()? renewToken,
    void Function(ErrorCode, String)? onAuthResult,
  })  : _getToken = getToken,
        _renewToken = renewToken,
        _onAuthResult = onAuthResult;

  final String Function()? _getToken;
  final Future<String?> Function()? _renewToken;
  final void Function(ErrorCode, String)? _onAuthResult;

  @override
  String getToken() => _getToken != null
      ? _getToken!()
      : (throw StateError('getToken not stubbed'));

  @override
  Future<String?> renewToken() =>
      _renewToken != null ? _renewToken!() : super.renewToken();

  @override
  void onAuthResult(ErrorCode code, String userId) =>
      _onAuthResult?.call(code, userId);
}

class _FnCommand extends CommandHandler {
  _FnCommand(this.name, [this._execute]);

  final bool Function(String args)? _execute;

  @override
  final String name;

  @override
  bool execute(String args, String senderId) => _execute?.call(args) ?? true;
}

class _ThrowingStore extends MessageStore {
  @override
  void save(chat.ChatMessage message) => throw StateError('store down');

  @override
  List<chat.ChatMessage> load(
          chat.ChannelType channelType, String channelId, int limit,
          {int? beforeTimestamp}) =>
      throw StateError('store down');
}

class _RecordingStore extends MessageStore {
  _RecordingStore({required this.reads, required this.cleanups});

  final void Function(String) reads;
  final void Function(int) cleanups;

  @override
  void save(chat.ChatMessage message) {}

  @override
  List<chat.ChatMessage> load(
          chat.ChannelType channelType, String channelId, int limit,
          {int? beforeTimestamp}) =>
      [];

  @override
  void markRead(
          chat.ChannelType channelType, String channelId, String messageId) =>
      reads(messageId);

  @override
  void cleanup(int olderThanMs) => cleanups(olderThanMs);
}
