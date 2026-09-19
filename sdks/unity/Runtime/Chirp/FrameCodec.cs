using System;
using System.Collections.Generic;

namespace Chirp.Sdk
{
    /// <summary>A corrupted frame stream cannot be resynchronized; the caller
    /// must treat the link as dead (same shape as the web/mobile ports).</summary>
    public sealed class FrameError : Exception
    {
        public FrameError(string message) : base(message) { }
    }

    /// <summary>
    /// Wire framing mirroring the C++ LengthPrefixedFramer
    /// (libs/network/length_prefixed_framer.cc): every frame is
    /// [u32_be payload_length][payload bytes], the prefix not counting itself.
    /// The server wraps this framer inside each WebSocket binary message, so
    /// the decoder tolerates chunks split or coalesced at any boundary.
    /// </summary>
    public static class FrameCodec
    {
        /// <summary>Defensive cap; the server itself only guards the u32 range.</summary>
        public const int MaxFrameBytes = 16 * 1024 * 1024;

        /// <summary>Wraps one payload into a full frame: [u32_be length][payload].</summary>
        public static byte[] Encode(byte[] payload)
        {
            if (payload.Length > MaxFrameBytes)
            {
                throw new FrameError($"frame payload too large: {payload.Length}");
            }
            var frame = new byte[4 + payload.Length];
            frame[0] = (byte)(payload.Length >> 24);
            frame[1] = (byte)(payload.Length >> 16);
            frame[2] = (byte)(payload.Length >> 8);
            frame[3] = (byte)payload.Length;
            Buffer.BlockCopy(payload, 0, frame, 4, payload.Length);
            return frame;
        }
    }

    /// <summary>Incremental decoder for the receive direction. Feed raw network
    /// chunks in any split; it returns every complete payload. A length prefix
    /// beyond <see cref="FrameCodec.MaxFrameBytes"/> throws FrameError and
    /// drops the buffer.</summary>
    public sealed class FrameDecoder
    {
        private byte[] _buf = Array.Empty<byte>();

        /// <summary>Bytes held waiting for the rest of a frame.</summary>
        public int Buffered => _buf.Length;

        public void Reset() => _buf = Array.Empty<byte>();

        public IReadOnlyList<byte[]> Feed(byte[] chunk)
        {
            var merged = new byte[_buf.Length + chunk.Length];
            Buffer.BlockCopy(_buf, 0, merged, 0, _buf.Length);
            Buffer.BlockCopy(chunk, 0, merged, _buf.Length, chunk.Length);
            _buf = merged;

            var frames = new List<byte[]>();
            while (_buf.Length >= 4)
            {
                var length = (_buf[0] << 24) | (_buf[1] << 16) | (_buf[2] << 8) | _buf[3];
                if (length > FrameCodec.MaxFrameBytes)
                {
                    _buf = Array.Empty<byte>();
                    throw new FrameError($"announced frame length {length} exceeds cap");
                }
                if (_buf.Length < 4 + length) break; // half frame: wait for more
                var payload = new byte[length];
                Buffer.BlockCopy(_buf, 4, payload, 0, length);
                frames.Add(payload);
                var rest = new byte[_buf.Length - 4 - length];
                Buffer.BlockCopy(_buf, 4 + length, rest, 0, rest.Length);
                _buf = rest;
            }
            return frames;
        }
    }
}
