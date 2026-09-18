// Wire framing, mirroring the C++ LengthPrefixedFramer
// (libs/network/length_prefixed_framer.cc): every frame is
//   [u32_be payload_length][payload bytes...]
// where the length prefix does NOT count itself. The server wraps the same
// framer inside each WebSocket binary message, and the future Dart port
// speaks the same bytes over plain TCP, so this decoder tolerates chunks
// split or coalesced at arbitrary boundaries.

/** Defensive cap; the server itself only guards the u32 range. */
export const MAX_FRAME_BYTES = 16 * 1024 * 1024;

export class FrameError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'FrameError';
  }
}

/** Wraps one payload into a full frame: [u32_be length][payload]. */
export function encodeFrame(payload: Uint8Array): Uint8Array {
  if (payload.length > MAX_FRAME_BYTES) {
    throw new FrameError(`frame payload too large: ${payload.length}`);
  }
  const frame = new Uint8Array(4 + payload.length);
  new DataView(frame.buffer).setUint32(0, payload.length, false);
  frame.set(payload, 4);
  return frame;
}

/**
 * Incremental decoder for the receive direction. Feed it raw network chunks
 * in any split; it hands back every complete payload. A length prefix beyond
 * MAX_FRAME_BYTES raises FrameError and drops the buffer — there is no way
 * to resynchronize a corrupted stream.
 */
export class FrameDecoder {
  private buf = new Uint8Array(0);

  feed(chunk: Uint8Array): Uint8Array[] {
    const merged = new Uint8Array(this.buf.length + chunk.length);
    merged.set(this.buf);
    merged.set(chunk, this.buf.length);
    this.buf = merged;

    const frames: Uint8Array[] = [];
    for (;;) {
      if (this.buf.length < 4) break;
      const length = new DataView(this.buf.buffer, this.buf.byteOffset).getUint32(0, false);
      if (length > MAX_FRAME_BYTES) {
        this.buf = new Uint8Array(0);
        throw new FrameError(`announced frame length ${length} exceeds cap`);
      }
      if (this.buf.length < 4 + length) break; // half frame: wait for more
      frames.push(this.buf.slice(4, 4 + length));
      this.buf = this.buf.slice(4 + length);
    }
    return frames;
  }

  /** Bytes held waiting for the rest of a frame. */
  buffered(): number {
    return this.buf.length;
  }

  reset(): void {
    this.buf = new Uint8Array(0);
  }
}
