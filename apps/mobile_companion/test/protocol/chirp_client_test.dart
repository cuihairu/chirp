import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/protocol/chirp_client.dart';
import 'package:chirp_mobile/protocol/errors.dart';
import 'package:chirp_mobile/protocol/msg_map.dart' as specs;

import 'fake_transport.dart';

ChirpClient connectedClient(FakeTransport transport) {
  final client = ChirpClient(
    url: 'ws://test',
    transportFactory: (_) => transport,
  );
  return client;
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
