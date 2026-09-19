import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:protobuf/protobuf.dart';

import 'chirp_client.dart';
import 'msg_map.dart';

/// Structural seam between the api layer and the connection so tests can
/// inject a scripted fake; ChirpClient implements it as-is.
abstract class ChatConnection {
  Future<void> connect();
  void disconnect();
  void resetBackoff();

  Future<TResp> request<TResp extends GeneratedMessage>(
    MessageSpec<TResp> spec,
    GeneratedMessage request, {
    int? timeoutMs,
  });

  void send(MsgID msgId, Uint8List body);

  /// Returns the unsubscribe function.
  void Function() onNotify(MsgID msgId, void Function(Uint8List) handler);
  void onStatus(void Function(ConnStatus) listener);
  void heartbeatNow();

  ConnStatus get status;
  bool get kicked;
}
