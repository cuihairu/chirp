import { describe, expect, it } from 'vitest';
import { FrameDecoder, FrameError, encodeFrame, MAX_FRAME_BYTES } from './frame';

describe('frame encoding', () => {
  it('prefixes the payload length, not counting the prefix itself', () => {
    const payload = new Uint8Array([1, 2, 3, 4, 5]);
    const frame = encodeFrame(payload);
    expect(frame.length).toBe(9);
    const view = new DataView(frame.buffer);
    expect(view.getUint32(0, false)).toBe(5);
    expect([...frame.slice(4)]).toEqual([1, 2, 3, 4, 5]);
  });

  it('encodes an empty payload', () => {
    const frame = encodeFrame(new Uint8Array(0));
    expect(frame.length).toBe(4);
    expect(new DataView(frame.buffer).getUint32(0, false)).toBe(0);
  });

  it('rejects oversized payloads before touching the wire', () => {
    expect(() => encodeFrame(new Uint8Array(MAX_FRAME_BYTES + 1))).toThrow(FrameError);
  });
});

describe('FrameDecoder', () => {
  it('decodes a well-formed frame', () => {
    const decoder = new FrameDecoder();
    const [payload] = decoder.feed(encodeFrame(new Uint8Array([9, 8, 7])));
    expect([...payload]).toEqual([9, 8, 7]);
    expect(decoder.buffered()).toBe(0);
  });

  it('reassembles a frame delivered one byte at a time', () => {
    const decoder = new FrameDecoder();
    const frame = encodeFrame(new Uint8Array([10, 20, 30]));
    const outputs: Uint8Array[][] = Array.from(frame, (byte) =>
      decoder.feed(new Uint8Array([byte])),
    );
    // Only the final byte completes the frame.
    expect(outputs.slice(0, -1).every((out) => out.length === 0)).toBe(true);
    expect([...outputs.at(-1)![0]]).toEqual([10, 20, 30]);
  });

  it('splits coalesced frames out of one chunk', () => {
    const decoder = new FrameDecoder();
    const merged = new Uint8Array(16);
    merged.set(encodeFrame(new Uint8Array([1, 2])));
    merged.set(encodeFrame(new Uint8Array([3, 4, 5, 6, 7, 8])), 6);
    const frames = decoder.feed(merged);
    expect(frames.length).toBe(2);
    expect([...frames[0]]).toEqual([1, 2]);
    expect([...frames[1]]).toEqual([3, 4, 5, 6, 7, 8]);
  });

  it('holds a partial tail until the rest arrives', () => {
    const decoder = new FrameDecoder();
    const frame = encodeFrame(new Uint8Array([1, 2, 3, 4]));
    const first = decoder.feed(frame.slice(0, 6));
    expect(first.length).toBe(0);
    expect(decoder.buffered()).toBe(6);
    const second = decoder.feed(frame.slice(6));
    expect(second.length).toBe(1);
    expect([...second[0]]).toEqual([1, 2, 3, 4]);
  });

  it('throws and drops the buffer on an impossible length prefix', () => {
    const decoder = new FrameDecoder();
    const evil = new Uint8Array([0xff, 0xff, 0xff, 0xff, 1, 2, 3]);
    expect(() => decoder.feed(evil)).toThrow(FrameError);
    expect(decoder.buffered()).toBe(0);
    // The decoder keeps working for the next frames on a fresh stream.
    const [payload] = decoder.feed(encodeFrame(new Uint8Array([42])));
    expect([...payload]).toEqual([42]);
  });

  it('starts over after reset', () => {
    const decoder = new FrameDecoder();
    decoder.feed(encodeFrame(new Uint8Array([1])).slice(0, 2));
    decoder.reset();
    expect(decoder.buffered()).toBe(0);
  });
});
