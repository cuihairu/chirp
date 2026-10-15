import { gzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';

import { GzipError, MAX_DECOMPRESSED_BYTES, gzipDecompress, setGzipDecompressor } from './gzip';

const encoder = new TextEncoder();
const text = (value: string) => encoder.encode(value);

/**
 * Realm-safe byte compare: the jsdom environment exposes its own Uint8Array
 * constructor, so a direct toEqual compares constructors instead of bytes.
 */
const expectBytes = (left: Uint8Array, right: Uint8Array) =>
  expect(Array.from(left)).toEqual(Array.from(right));

// node:zlib builds the fixtures; the code under test only ever decodes, which
// keeps the browser/runtime path dependency-free.
const compressed = (value: Uint8Array) => gzipSync(value);

describe('gzipDecompress', () => {
  it('roundtrips a gzip container', async () => {
    const payload = text('chirp history payload with 中文 and repeats');
    expectBytes(await gzipDecompress(compressed(payload)), payload);
  });

  it('roundtrips a payload large enough to span several chunks', async () => {
    const payload = new Uint8Array(300_000);
    for (let i = 0; i < payload.length; i += 1) {
      payload[i] = (i * 31) & 0xff;
    }
    expectBytes(await gzipDecompress(compressed(payload)), payload);
  });

  it('rejects empty input', async () => {
    await expect(gzipDecompress(new Uint8Array(0))).rejects.toThrow(GzipError);
  });

  it('rejects a corrupt container', async () => {
    const bytes = compressed(text('abcdef'.repeat(200)));
    bytes[Math.floor(bytes.length / 2)] ^= 0xff;
    await expect(gzipDecompress(bytes)).rejects.toThrow(GzipError);
  });

  it('rejects a truncated container', async () => {
    const bytes = compressed(text('abcdef'.repeat(500)));
    await expect(gzipDecompress(bytes.slice(0, Math.floor(bytes.length / 2)))).rejects.toThrow(
      GzipError,
    );
  });

  it('enforces the decompression cap', async () => {
    // 17 MiB of zeros compresses to a few kilobytes but inflates past the cap.
    const bomb = new Uint8Array(MAX_DECOMPRESSED_BYTES + 1024 * 1024);
    await expect(gzipDecompress(compressed(bomb))).rejects.toThrow(/cap/);
  });
});

describe('setGzipDecompressor', () => {
  it('routes gzipDecompress through an injected platform decoder', async () => {
    const input = compressed(text('the built-in decoder never sees this'));
    const seen: Uint8Array[] = [];
    setGzipDecompressor(async (bytes) => {
      seen.push(bytes);
      return text('platform decoder payload');
    });
    try {
      expectBytes(await gzipDecompress(input), text('platform decoder payload'));
      expect(seen).toHaveLength(1);
      expectBytes(seen[0], input);
    } finally {
      setGzipDecompressor(null);
    }
  });

  it('restores the built-in decoder when passed null', async () => {
    setGzipDecompressor(async () => text('stub that would fail a real container'));
    setGzipDecompressor(null);
    const payload = text('back on the DecompressionStream decoder');
    expectBytes(await gzipDecompress(compressed(payload)), payload);
  });
});
