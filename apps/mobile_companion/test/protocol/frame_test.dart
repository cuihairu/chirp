import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/protocol/frame.dart';

Uint8List frameOf(String payload) {
  final body = Uint8List.fromList(payload.codeUnits);
  final out = Uint8List(4 + body.length);
  ByteData.view(out.buffer).setUint32(0, body.length, Endian.big);
  out.setAll(4, body);
  return out;
}

void main() {
  test('encode/decode roundtrip on a clean stream', () {
    final decoder = FrameDecoder();
    final frames =
        decoder.feed(encodeFrame(Uint8List.fromList('hello'.codeUnits)));
    expect(frames, hasLength(1));
    expect(frames.single, Uint8List.fromList('hello'.codeUnits));
  });

  test('coalesced frames split; split frames reassemble', () {
    final decoder = FrameDecoder();
    final a = frameOf('aaaa');
    final b = frameOf('bb');
    // Two frames in one chunk.
    final both = Uint8List.fromList([...a, ...b]);
    expect(decoder.feed(both).map(String.fromCharCodes), ['aaaa', 'bb']);

    // One frame across three chunks.
    decoder.reset();
    final single = frameOf('hello world');
    final hit = <Uint8List>[];
    final cut = single.length ~/ 3;
    hit.addAll(decoder.feed(Uint8List.sublistView(single, 0, cut)));
    hit.addAll(decoder.feed(Uint8List.sublistView(single, cut, 2 * cut)));
    expect(hit, isEmpty); // no premature delivery
    hit.addAll(decoder.feed(Uint8List.sublistView(single, 2 * cut)));
    expect(hit.map(String.fromCharCodes), ['hello world']);
  });

  test('oversized frame throws and leaves the decoder reusable after reset',
      () {
    final decoder = FrameDecoder();
    final huge = Uint8List(4 + maxFrameBytes + 1);
    ByteData.view(huge.buffer).setUint32(0, maxFrameBytes + 1, Endian.big);
    expect(() => decoder.feed(huge), throwsFrameError);
    decoder.reset();
    expect(decoder.feed(frameOf('ok')).map(String.fromCharCodes), ['ok']);
  });

  test('length prefix does not count itself', () {
    final encoded = encodeFrame(Uint8List.fromList([1, 2, 3]));
    expect(encoded.length, 7);
    expect(ByteData.view(encoded.buffer).getUint32(0, Endian.big), 3);
  });
}

Matcher get throwsFrameError => throwsA(isA<FrameError>());
