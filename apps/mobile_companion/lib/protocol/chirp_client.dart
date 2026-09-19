import 'dart:async';
import 'dart:math';
import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:fixnum/fixnum.dart';
import 'package:protobuf/protobuf.dart';

import 'errors.dart';
import 'chat_connection.dart';
import 'frame.dart';
import 'msg_map.dart';
import 'ws_transport.dart';

/// Connection state machine:
///   idle → connecting → connected ⇄ waitingReconnect (auto reconnect)
///                     ↘ kicked        (KICK_NOTIFY: terminal, no reconnect)
///   any  → closed        (manual disconnect(); terminal until connect())
enum ConnStatus {
  idle,
  connecting,
  connected,
  waitingReconnect,
  closed,
  kicked,
}

class ChirpClientOptions {
  const ChirpClientOptions({
    this.heartbeatIntervalMs = 25000,
    this.maxMissedPongs = 2,
    this.requestTimeoutMs = 10000,
    this.reconnectBaseMs = 500,
    this.reconnectMaxMs = 15000,
    this.jitterRatio = 0.2,
  });

  /// Server has no idle kick; 25s pings keep intermediaries honest.
  final int heartbeatIntervalMs;

  /// Die after this many consecutive pings without any pong.
  final int maxMissedPongs;
  final int requestTimeoutMs;
  final int reconnectBaseMs;
  final int reconnectMaxMs;

  /// ± fraction applied to each reconnect delay.
  final double jitterRatio;
}

class _Pending {
  _Pending(this.completer, this.timer);

  final Completer<Uint8List> completer;
  final Timer timer;
}

/// Typed websocket client for the Packet protocol: framing, request/response
/// correlation by sequence, heartbeats, auto reconnect with backoff and
/// terminal kick handling. Port of the web companion's ChirpClient.
class ChirpClient implements ChatConnection {
  ChirpClient({
    required this.url,
    WsTransport Function(String url)? transportFactory,
    this.options = const ChirpClientOptions(),
    Random? random,
  })  : transportFactory = transportFactory ?? ((url) => IoWebSocket()),
        _random = random ?? Random();

  final String url;
  final WsTransport Function(String url) transportFactory;
  final ChirpClientOptions options;
  final Random _random;

  WsTransport? _ws;
  final FrameDecoder _decoder = FrameDecoder();
  StreamSubscription<Uint8List>? _messageSub;
  StreamSubscription<void>? _closeSub;
  final Map<int, _Pending> _pending = {};
  // MsgID is a ProtobufEnum: identity == / hashCode make it map-safe.
  final Map<MsgID, Set<void Function(Uint8List)>> _notifyHandlers = {};
  final List<void Function(ConnStatus)> _statusListeners = [];

  ConnStatus _status = ConnStatus.idle;
  int _seqCounter = 0;
  int _attempt = 0;
  int _missedPongs = 0;
  bool _kicked = false;

  Timer? _reconnectTimer;
  Timer? _pingTimer;

  @override
  ConnStatus get status => _status;

  /// True after the server sent KICK_NOTIFY; auto reconnect stays off.
  @override
  bool get kicked => _kicked;

  /// server_time − local_time at the last pong; null before the first.
  int? clockOffsetMs;

  @override
  void onStatus(void Function(ConnStatus) listener) {
    _statusListeners.add(listener);
  }

  /// Subscribe to server pushes (sequence === 0 packets). Unknown msgIds are
  /// ignored, so subscribing to one is purely optional. Returns the
  /// unsubscribe function.
  @override
  void Function() onNotify(MsgID msgId, void Function(Uint8List) handler) {
    final handlers = _notifyHandlers[msgId] ??= <void Function(Uint8List)>{};
    handlers.add(handler);
    return () => handlers.remove(handler);
  }

  /// Opens the socket. Completes on open, errors if it closes first.
  @override
  Future<void> connect() {
    // An explicit connect() is a user action: clear kick/terminal state and
    // let the backoff start over.
    _kicked = false;
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    final stale = _ws;
    _ws = null;
    if (stale != null) {
      _teardownSubs();
      stale.close();
    }

    final completer = Completer<void>();
    _setStatus(ConnStatus.connecting);
    final ws = transportFactory(url);
    _ws = ws;
    var settled = false;

    Future<void> openIt() async {
      try {
        await ws.open(url);
      } catch (_) {
        if (!settled) {
          settled = true;
          completer.completeError(RequestError(RequestErrorKind.closed));
        }
        _handleClose();
        return;
      }
      if (settled) return; // disconnect() raced the open
      settled = true;
      _missedPongs = 0;
      _setStatus(ConnStatus.connected);
      _startHeartbeat();
      _messageSub = ws.binaryMessages.listen(_handleData);
      _closeSub = ws.closed.listen((_) => _handleClose());
      completer.complete();
    }

    unawaited(openIt());
    return completer.future;
  }

  /// Manual close; no reconnect follows.
  @override
  void disconnect() {
    _setStatus(ConnStatus.closed);
    _clearHeartbeat();
    _rejectAllPending(RequestErrorKind.closed);
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _teardownSubs();
    _ws?.close();
    _ws = null;
  }

  /// Reset the reconnect backoff after a successful login. Opening the socket
  /// alone proves little (the server may still refuse the token), so the api
  /// layer calls this once the LOGIN round-trip succeeded.
  @override
  void resetBackoff() {
    _attempt = 0;
  }

  /// Immediate heartbeat (mobile app lifecycle resume).
  @override
  void heartbeatNow() {
    if (_status == ConnStatus.connected) {
      _sendPing();
    }
  }

  /// Typed request/response. Throws RequestError(closed) when not connected,
  /// (timeout) on deadline; server error codes are decided by the api layer
  /// from the decoded response.
  @override
  Future<TResp> request<TResp extends GeneratedMessage>(
    MessageSpec<TResp> spec,
    GeneratedMessage request, {
    int? timeoutMs,
  }) async {
    if (_status != ConnStatus.connected || _ws == null) {
      throw RequestError(RequestErrorKind.closed);
    }
    final sequence = ++_seqCounter;
    final body = await _rawRequest(
        spec.reqMsgId, Uint8List.fromList(request.writeToBuffer()), sequence,
        timeoutMs: timeoutMs ?? options.requestTimeoutMs);
    return spec.decodeResponse(body);
  }

  /// Fire-and-forget send for messages that never get a response frame
  /// (MESSAGE_ACK 2209, TYPING 2208). Throws RequestError(closed) when not
  /// connected.
  @override
  void send(MsgID msgId, Uint8List body) {
    if (_status != ConnStatus.connected || _ws == null) {
      throw RequestError(RequestErrorKind.closed);
    }
    _rawSend(msgId, body, ++_seqCounter);
  }

  Future<Uint8List> _rawRequest(MsgID msgId, Uint8List body, int sequence,
      {required int timeoutMs}) {
    final completer = Completer<Uint8List>();
    final pending = _Pending(
      completer,
      Timer(Duration(milliseconds: timeoutMs), () {
        _pending.remove(sequence);
        if (!completer.isCompleted) {
          completer.completeError(RequestError(RequestErrorKind.timeout));
        }
      }),
    );
    _pending[sequence] = pending;
    try {
      _rawSend(msgId, body, sequence);
    } catch (error) {
      pending.timer.cancel();
      _pending.remove(sequence);
      if (!completer.isCompleted) {
        completer.completeError(
          error is RequestError ? error : RequestError(RequestErrorKind.closed),
        );
      }
    }
    return completer.future;
  }

  void _setStatus(ConnStatus status) {
    if (_status == status) return;
    _status = status;
    for (final listener in List.of(_statusListeners)) {
      listener(status);
    }
  }

  void _rawSend(MsgID msgId, Uint8List body, int sequence) {
    final ws = _ws;
    if (ws == null) throw RequestError(RequestErrorKind.closed);
    final packet = Packet(
      msgId: msgId,
      sequence: Int64(sequence),
      body: body,
    );
    ws.send(encodeFrame(Uint8List.fromList(packet.writeToBuffer())));
  }

  void _startHeartbeat() {
    _clearHeartbeat();
    _pingTimer = Timer.periodic(
      Duration(milliseconds: options.heartbeatIntervalMs),
      (_) => _sendPing(),
    );
  }

  void _clearHeartbeat() {
    _pingTimer?.cancel();
    _pingTimer = null;
  }

  void _sendPing() {
    if (_missedPongs >= options.maxMissedPongs) {
      // Consecutive missed pongs: the link is dead in practice — close it and
      // let the reconnect path take over.
      _ws?.close();
      return;
    }
    _missedPongs++;
    final ping =
        HeartbeatPing(timestamp: Int64(DateTime.now().millisecondsSinceEpoch));
    try {
      _rawSend(MsgID.HEARTBEAT_PING, Uint8List.fromList(ping.writeToBuffer()),
          ++_seqCounter);
    } catch (_) {
      // Socket died underneath us; the closed event will fire.
    }
  }

  void _handleData(Uint8List bytes) {
    List<Uint8List> frames;
    try {
      frames = _decoder.feed(bytes);
    } on FrameError {
      // A corrupted stream cannot be resynchronized; treat it as a dead link.
      _ws?.close();
      return;
    }
    for (final frame in frames) {
      _handleFrame(frame);
    }
  }

  void _handleFrame(Uint8List frame) {
    Packet packet;
    try {
      packet = Packet.fromBuffer(frame);
    } catch (_) {
      return; // undecodable packet: ignore rather than kill the connection
    }
    if (packet.sequence == Int64.ZERO) {
      _dispatchNotify(packet.msgId, Uint8List.fromList(packet.body));
      return;
    }
    if (packet.msgId == MsgID.HEARTBEAT_PONG) {
      // Pong echoes the ping's sequence and is not in the pending table.
      _onPong(Uint8List.fromList(packet.body));
      return;
    }
    final entry = _pending.remove(packet.sequence.toInt());
    if (entry == null) return; // response to an already timed-out request
    entry.timer.cancel();
    entry.completer.complete(Uint8List.fromList(packet.body));
  }

  void _onPong(Uint8List body) {
    _missedPongs = 0;
    try {
      final pong = HeartbeatPong.fromBuffer(body);
      clockOffsetMs =
          pong.serverTime.toInt() - DateTime.now().millisecondsSinceEpoch;
    } catch (_) {
      // A malformed pong still proves the link is alive.
    }
  }

  void _dispatchNotify(MsgID msgId, Uint8List body) {
    if (msgId == MsgID.KICK_NOTIFY) {
      _markKicked();
      return;
    }
    final handlers = _notifyHandlers[msgId];
    if (handlers == null) return; // unknown or uninteresting msgId
    for (final handler in List.of(handlers)) {
      try {
        handler(body);
      } catch (_) {
        // One bad handler must not starve the others.
      }
    }
  }

  void _markKicked() {
    // KICK then reconnect would just fight the new device, so auto reconnect
    // stays off and the user is sent back to login. The status flip happens
    // HERE, not via the close event: subscriptions are already torn down by
    // the time the transport announces the close.
    _kicked = true;
    _clearHeartbeat();
    _rejectAllPending(RequestErrorKind.kicked);
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _setStatus(ConnStatus.kicked);
    _teardownSubs();
    _ws?.close();
    _ws = null;
  }

  void _handleClose() {
    _teardownSubs();
    _ws = null;
    _clearHeartbeat();
    _rejectAllPending(RequestErrorKind.closed);
    _decoder.reset();
    if (_kicked) {
      _setStatus(ConnStatus.kicked);
      return;
    }
    if (_status == ConnStatus.closed) return; // manual disconnect()
    _setStatus(ConnStatus.waitingReconnect);
    _scheduleReconnect();
  }

  void _rejectAllPending(RequestErrorKind kind) {
    for (final entry in _pending.values) {
      entry.timer.cancel();
      if (!entry.completer.isCompleted) {
        entry.completer.completeError(RequestError(kind));
      }
    }
    _pending.clear();
  }

  void _scheduleReconnect() {
    final base = min(
      options.reconnectBaseMs * pow(2, _attempt).toInt(),
      options.reconnectMaxMs,
    );
    final jitter = (base * options.jitterRatio).round();
    final delay = base + _random.nextInt(jitter * 2 + 1) - jitter;
    _attempt++;
    _reconnectTimer = Timer(Duration(milliseconds: delay), () {
      _reconnectTimer = null;
      // A failed attempt loops straight back through _handleClose.
      unawaited(connect().catchError((Object _) {}));
    });
  }

  void _teardownSubs() {
    _messageSub?.cancel();
    _messageSub = null;
    _closeSub?.cancel();
    _closeSub = null;
  }
}
