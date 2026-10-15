/**
 * gzip reader for the history-readback compression shape (game_chat_features
 * P2 消息压缩). A session that declared supports_gzip at login gets the history
 * message list inside GetHistoryResponse.messagesGzip: a gzip (RFC 1952)
 * container holding a same-shape GetHistoryResponse. DecompressionStream is
 * the default cross-runtime decoder (browsers, Node >= 18, jsdom), so no
 * dependency is added; the container format is the reason every runtime reads
 * it unconfigured. Runtimes without that web API (ArkTS) install their own
 * platform decoder through setGzipDecompressor.
 */

/** Decompression-bomb cap; the C++ core primitive uses the same bound. */
export const MAX_DECOMPRESSED_BYTES = 16 * 1024 * 1024;

/** Raised for empty input, a corrupt/truncated container, or the size cap. */
export class GzipError extends Error {}

/** Platform decoder shape: raw gzip container in, inflated bytes out. */
export type GzipDecompressor = (bytes: Uint8Array) => Promise<Uint8Array>;

// Active decoder; starts on the DecompressionStream implementation below.
let decompressor: GzipDecompressor = webGzipDecompress;

/**
 * Injection seam for the history-decompression consumer. Pass `null` to
 * restore the built-in DecompressionStream decoder. Hosts whose runtime lacks
 * that API (ArkTS ships no Compression Streams) install their platform
 * decoder once at startup — the message spec itself stays untouched.
 */
export function setGzipDecompressor(fn: GzipDecompressor | null): void {
  decompressor = fn ?? webGzipDecompress;
}

/** Inflate a gzip container through the active decoder. */
export function gzipDecompress(bytes: Uint8Array): Promise<Uint8Array> {
  return decompressor(bytes);
}

/**
 * Default decoder: streaming the transform keeps the memory bound — the
 * reader bails as soon as the inflated size crosses the cap instead of
 * buffering an unbounded body.
 */
async function webGzipDecompress(bytes: Uint8Array): Promise<Uint8Array> {
  if (bytes.length === 0) {
    throw new GzipError('gzip: empty input');
  }

  // BufferSource source type matches DecompressionStream's writable side.
  const source = new ReadableStream<BufferSource>({
    start(controller) {
      // A fresh ArrayBuffer-backed view: the decoder's writable side refuses
      // views over SharedArrayBuffer in the current lib dom typings.
      controller.enqueue(new Uint8Array(bytes));
      controller.close();
    },
  });
  const stream = source.pipeThrough(new DecompressionStream('gzip'));

  const reader = stream.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (value === undefined) continue;
      total += value.byteLength;
      if (total > MAX_DECOMPRESSED_BYTES) {
        throw new GzipError('gzip: decompressed size exceeds the cap');
      }
      chunks.push(value);
    }
  } catch (err) {
    // Cancel the pipe so a corrupt or truncated container cannot leave the
    // transform pending on more input; the original error wins.
    await reader.cancel().catch(() => undefined);
    if (err instanceof GzipError) throw err;
    throw new GzipError('gzip: corrupt or truncated container');
  }

  const out = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    out.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return out;
}
