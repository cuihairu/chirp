using System;
using System.IO;
using System.IO.Compression;

namespace Chirp.Sdk
{
    /// <summary>
    /// Raised for empty input, a corrupt/truncated container, or the
    /// decompressed-size cap. Surfaces the same way the C++ core's
    /// BadResponse and the TS clients' GzipError do: the caller sees the
    /// history readback fail rather than silently losing messages.
    /// </summary>
    public sealed class GzipInflateError : Exception
    {
        public GzipInflateError(string message) : base(message)
        {
        }
    }

    /// <summary>
    /// gzip reader for the history-readback compression shape
    /// (game_chat_features P2 消息压缩). A session that declared
    /// supports_gzip at login gets the history message list inside
    /// GetHistoryResponse.MessagesGzip: a gzip (RFC 1952) container holding
    /// a same-shape GetHistoryResponse. GZipStream reads RFC 1952 with no
    /// configuration, which is why every runtime consumes the container
    /// unchanged.
    /// </summary>
    public static class GzipInflate
    {
        /// <summary>Decompression-bomb cap; the C++ and TS primitives use the
        /// same 16 MiB bound.</summary>
        public const int MaxDecompressedBytes = 16 * 1024 * 1024;

        /// <summary>Smallest legal container: the 10-byte RFC 1952 header,
        /// an empty deflate final block, and the 8-byte trailer.</summary>
        private const int MinContainerBytes = 18;

        /// <summary>
        /// Inflate a gzip container. Streaming the transform keeps the memory
        /// bound: the reader aborts as soon as the inflated size crosses the
        /// cap instead of buffering an unbounded body.
        /// </summary>
        /// <remarks>
        /// GZipStream validates the CRC/ISIZE trailer only once the deflate
        /// final block is reached, so a container cut short mid-body reads back
        /// as a silently short message list. The trailer is checked here
        /// explicitly, which keeps the reject-on-truncation parity the C++ core
        /// and the TS clients already have.
        /// </remarks>
        public static byte[] Inflate(byte[] container)
        {
            if (container == null || container.Length == 0)
            {
                throw new GzipInflateError("gzip: empty input");
            }
            if (container.Length < MinContainerBytes)
            {
                throw new GzipInflateError("gzip: corrupt or truncated container");
            }

            var output = new MemoryStream();
            try
            {
                using (var source = new MemoryStream(container, writable: false))
                using (var deflate = new GZipStream(source, CompressionMode.Decompress))
                {
                    var buffer = new byte[8192];
                    int read;
                    while ((read = deflate.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        output.Write(buffer, 0, read);
                        if (output.Length > MaxDecompressedBytes)
                        {
                            throw new GzipInflateError("gzip: decompressed size exceeds the cap");
                        }
                    }
                }
            }
            catch (GzipInflateError)
            {
                throw;
            }
            catch (Exception error) when (error is IOException || error is InvalidDataException)
            {
                throw new GzipInflateError("gzip: corrupt or truncated container");
            }
            var inflated = output.ToArray();
            VerifyTrailer(container, inflated);
            return inflated;
        }

        private static void VerifyTrailer(byte[] container, byte[] inflated)
        {
            var trailer = container.Length - 8;
            var expectedCrc = ReadUInt32(container, trailer);
            var expectedSize = ReadUInt32(container, trailer + 4);
            if (expectedSize != (uint)inflated.Length || expectedCrc != Crc32(inflated))
            {
                throw new GzipInflateError("gzip: corrupt or truncated container");
            }
        }

        /// <summary>RFC 1952 trailer fields are little-endian.</summary>
        private static uint ReadUInt32(byte[] data, int offset)
        {
            return (uint)(data[offset]
                | (data[offset + 1] << 8)
                | (data[offset + 2] << 16)
                | (data[offset + 3] << 24));
        }

        private static readonly uint[] CrcTable = BuildCrcTable();

        private static uint[] BuildCrcTable()
        {
            var table = new uint[256];
            for (uint index = 0; index < 256; index++)
            {
                var value = index;
                for (var bit = 0; bit < 8; bit++)
                {
                    value = (value & 1) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
                }
                table[index] = value;
            }
            return table;
        }

        /// <summary>CRC-32/ISO-HDLC, the checksum the gzip trailer carries.
        /// Written out here so the strict check needs no package.</summary>
        private static uint Crc32(byte[] data)
        {
            var crc = 0xFFFFFFFFu;
            foreach (var value in data)
            {
                crc = CrcTable[(crc ^ value) & 0xFF] ^ (crc >> 8);
            }
            return crc ^ 0xFFFFFFFFu;
        }
    }
}
