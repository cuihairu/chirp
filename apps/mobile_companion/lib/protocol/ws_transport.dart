import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

/// Minimal transport seam between the connection state machine and the
/// platform socket, mirroring the web port's WebSocketLike injection point.
/// Tests inject a scripted fake; production uses [IoWebSocket].
abstract class WsTransport {
  /// Opens the socket; completes when open, errors when the attempt failed.
  Future<void> open(String url);

  /// Binary frames from the server. Text frames never occur on this
  /// protocol; implementations drop them.
  Stream<Uint8List> get binaryMessages;

  /// Fires exactly once when the socket is down — after [close], after a
  /// remote close, or after an error. Never fires before [open] completes.
  Stream<void> get closed;

  void send(List<int> data);

  /// Best-effort graceful close; [closed] still fires.
  void close();
}

/// dart:io WebSocket adapter.
class IoWebSocket implements WsTransport {
  WebSocket? _ws;
  final StreamController<void> _closedController = StreamController.broadcast();
  bool _closeAnnounced = false;

  @override
  Future<void> open(String url) async {
    final ws = await WebSocket.connect(url);
    _ws = ws;
    unawaited(ws.done.whenComplete(() {
      if (!_closeAnnounced) {
        _closeAnnounced = true;
        _closedController.add(null);
        _closedController.close();
      }
    }));
  }

  @override
  Stream<Uint8List> get binaryMessages {
    final ws = _ws;
    if (ws == null) {
      throw StateError('open() must complete before reading messages');
    }
    // Text frames are not part of this protocol; drop them.
    return ws.where((data) => data is Uint8List).cast<Uint8List>();
  }

  @override
  Stream<void> get closed => _closedController.stream;

  @override
  void send(List<int> data) {
    _ws?.add(data);
  }

  @override
  void close() {
    unawaited(_ws?.close());
    // closed fires from ws.done; if open() never completed there is nothing
    // to announce.
  }
}
