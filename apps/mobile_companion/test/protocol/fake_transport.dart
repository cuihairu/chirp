import 'dart:async';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:protobuf/protobuf.dart';

import 'package:chirp_mobile/protocol/frame.dart';
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

/// Answers whatever request the client sent last (login, send, …).
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
