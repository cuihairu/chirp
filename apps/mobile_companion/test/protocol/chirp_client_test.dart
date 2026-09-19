import 'dart:async';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/protocol/chirp_client.dart';
import 'package:chirp_mobile/protocol/errors.dart';
import 'package:chirp_mobile/protocol/frame.dart';
import 'package:chirp_mobile/protocol/msg_map.dart' as specs;
import 'package:protobuf/protobuf.dart';
import 'package:chirp_mobile/protocol/ws_transport.dart';

/// Scriptable in-memory transport: the test plays the server side.
class FakeTransport implements WsTransport {
  final sent = <Uint8List>[];
  final _messages = StreamController<Uint8List>.broadcast();
  final _closed = StreamController<void>.broadcast();
  int closeCount = 0;

  @override
  Future<void> open(String url) async {}

  @override
  Stream<Uint8List> get binaryMessages => _messages.stream;

  @override
  Stream<void> get closed => _closed.stream;

  @override
  void send(List<int> data) => sent.add(Uint8List.fromList(data));

  @override
  void close() {
    closeCount++;
    _closed.add(null);
  }

  /// Server → client push, framed.
  void serverPacket(Packet packet) {
    _messages.add(encodeFrame(packet.writeToBuffer()));
  }

  /// Last client → server frame, decoded back into a Packet.
  Packet lastSentPacket() {
    final decoder = FrameDecoder();
    final frames = decoder.feed(sent.last);
    return Packet.fromBuffer(frames.single);
  }

  Future<void> settle() async {
    await pumpEventQueue();
  }
}

ChirpClient connectedClient(FakeTransport transport) {
  final client = ChirpClient(
    url: 'ws://test',
    transportFactory: (_) => transport,
  );
  return client;
}

Future<Packet> respond(FakeTransport transport, int expectSeq, MsgID respId,
    GeneratedMessage body) async {
  // The client's request arrives with the next outgoing sequence.
  await transport.settle();
  final request = transport.lastSentPacket();
  expect(request.sequence, Int64(expectSeq));
  transport.serverPacket(Packet(
    msgId: respId,
    sequence: Int64(expectSeq),
    body: body.writeToBuffer(),
  ));
  return request;
}

void main() {
  test('connect reaches connected and fires status listeners', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    final statuses = <ConnStatus>[];
    client.onStatus(statuses.add);
    await client.connect();
    expect(client.status, ConnStatus.connected);
    expect(statuses, [ConnStatus.connecting, ConnStatus.connected]);
  });

  test('request/response correlates by sequence', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();

    final future = client.request(
      specs.sendMessage,
      SendMessageRequest(senderId: 'u1'),
    );
    await respond(transport, 1, MsgID.SEND_MESSAGE_RESP,
        SendMessageResponse(code: ErrorCode.OK, messageId: 'm1'));
    final resp = await future;
    expect(resp.code, ErrorCode.OK);
    expect(resp.messageId, 'm1');

    final packet = transport.lastSentPacket();
    expect(packet.msgId, MsgID.SEND_MESSAGE_REQ);
    expect(packet.body, SendMessageRequest(senderId: 'u1').writeToBuffer());
  });

  test('request times out; a late response is dropped harmlessly', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();

    final future = client.request(
      specs.getHistory,
      GetHistoryRequest(userId: 'u1'),
      timeoutMs: 10,
    );
    await expectLater(
        future,
        throwsA(isA<RequestError>().having(
          (e) => e.kind,
          'kind',
          RequestErrorKind.timeout,
        )));
    // The straggler now finds no pending entry and must not crash.
    transport.serverPacket(Packet(
      msgId: MsgID.GET_HISTORY_RESP,
      sequence: Int64(99),
      body: GetHistoryResponse().writeToBuffer(),
    ));
    await transport.settle();
  });

  test('notify packets (sequence 0) dispatch to subscribers', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();

    final bodies = <Uint8List>[];
    final off = client.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, bodies.add);
    transport.serverPacket(Packet(
      msgId: MsgID.CHAT_MESSAGE_NOTIFY,
      body: ChatMessage(messageId: 'm9').writeToBuffer(),
    ));
    await transport.settle();
    expect(bodies, hasLength(1));
    expect(ChatMessage.fromBuffer(bodies.single).messageId, 'm9');

    off();
    transport.serverPacket(Packet(
      msgId: MsgID.CHAT_MESSAGE_NOTIFY,
      body: ChatMessage(messageId: 'm10').writeToBuffer(),
    ));
    await transport.settle();
    expect(bodies, hasLength(1));
  });

  test('kick notify is terminal: pending rejected, no reconnect', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();

    final future =
        client.request(specs.login, LoginRequest(token: 'u1', deviceId: 'd1'));
    await transport.settle();

    transport.serverPacket(Packet(
      msgId: MsgID.KICK_NOTIFY,
      body: KickNotify(reason: 'device takeover').writeToBuffer(),
    ));
    await expectLater(
        future,
        throwsA(isA<RequestError>().having(
          (e) => e.kind,
          'kind',
          RequestErrorKind.kicked,
        )));
    await transport.settle();
    expect(client.kicked, isTrue);
    expect(client.status, ConnStatus.kicked);
    expect(transport.closeCount, 1);
  });

  test('heartbeat ping/pong tracks the clock offset', () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();
    expect(client.clockOffsetMs, isNull);

    client.heartbeatNow();
    await transport.settle();
    final ping = transport.lastSentPacket();
    expect(ping.msgId, MsgID.HEARTBEAT_PING);

    final now = DateTime.now().millisecondsSinceEpoch;
    transport.serverPacket(Packet(
      msgId: MsgID.HEARTBEAT_PONG,
      // The pong must echo the ping's non-zero sequence, else the client
      // treats it as a notify and drops it.
      sequence: ping.sequence,
      body: HeartbeatPong(serverTime: Int64(now)).writeToBuffer(),
    ));
    await transport.settle();
    expect(client.clockOffsetMs, isNotNull);
    expect(client.clockOffsetMs!.abs(), lessThan(2000));
  });

  test('transport close waits to reconnect; disconnect stops everything',
      () async {
    final transport = FakeTransport();
    final client = connectedClient(transport);
    await client.connect();

    transport.close();
    await transport.settle();
    expect(client.status, ConnStatus.waitingReconnect);

    // A reconnect was scheduled; disconnect() must cancel it cleanly.
    client.disconnect();
    expect(client.status, ConnStatus.closed);
  });
}
