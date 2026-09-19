// Wire framing, mirroring the C++ LengthPrefixedFramer
// (libs/network/length_prefixed_framer.cc): every frame is
//   [u32_be payload_length][payload bytes...]
// where the length prefix does NOT count itself. The server wraps the same
// framer inside each WebSocket binary message, so this decoder tolerates
// chunks split or coalesced at arbitrary boundaries.
import 'dart:typed_data';

/// Defensive cap; the server itself only guards the u32 range.
const int maxFrameBytes = 16 * 1024 * 1024;

/// A corrupted stream cannot be resynchronized; callers treat it as a dead
/// link (the web port throws the same-shaped error).
class FrameError implements Exception {
  FrameError(this.message);

  final String message;

  @override
  String toString() => 'FrameError: $message';
}

/// Wraps one payload into a full frame: [u32_be length][payload].
Uint8List encodeFrame(Uint8List payload) {
  if (payload.length > maxFrameBytes) {
    throw FrameError('frame payload too large: ${payload.length}');
  }
  final frame = Uint8List(4 + payload.length);
  ByteData.view(frame.buffer).setUint32(0, payload.length, Endian.big);
  frame.setAll(4, payload);
  return frame;
}

/// Incremental decoder for the receive direction. Feed it raw network chunks
/// in any split; it hands back every complete payload. A length prefix beyond
/// [maxFrameBytes] raises FrameError and drops the buffer.
class FrameDecoder {
  Uint8List _buf = Uint8List(0);

  List<Uint8List> feed(Uint8List chunk) {
    final merged = Uint8List(_buf.length + chunk.length);
    merged.setAll(0, _buf);
    merged.setAll(_buf.length, chunk);
    _buf = merged;

    final frames = <Uint8List>[];
    for (;;) {
      if (_buf.length < 4) break;
      final length = ByteData.view(_buf.buffer, _buf.offsetInBytes)
          .getUint32(0, Endian.big);
      if (length > maxFrameBytes) {
        _buf = Uint8List(0);
        throw FrameError('announced frame length $length exceeds cap');
      }
      if (_buf.length < 4 + length) break; // half frame: wait for more
      frames.add(_buf.sublist(4, 4 + length));
      _buf = _buf.sublist(4 + length);
    }
    return frames;
  }

  /// Bytes held waiting for the rest of a frame.
  int get buffered => _buf.length;

  void reset() {
    _buf = Uint8List(0);
  }
}
