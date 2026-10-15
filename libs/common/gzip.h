#pragma once

#include <cstddef>
#include <string>

// gzip (RFC 1952 container) round-trip for the history-readback compression
// capability (game_chat_features P2 消息压缩, 2026-10-15): the chat server
// compresses GetHistoryResponse message lists for sessions that declared
// LoginRequest.supports_gzip, and the core SDK decompresses them. The gzip
// container is chosen over the raw zlib wrapper so every language runtime
// accepts it unconfigured — TS DecompressionStream('gzip'), C# GZipStream,
// Go compress/gzip, Java GZIPInputStream — while inflate auto-detects it
// here (windowBits 15+32).

namespace chirp::common {

// Decompressed output above this cap fails the decompress - the
// decompression-bomb guard, same order of magnitude as the 16 MiB wire-frame
// cap the client protocols enforce.
inline constexpr size_t kMaxDecompressedBytes = 16 * 1024 * 1024;

// Gzip-compresses input into output (RFC 1952 container). Returns false on
// empty input or a zlib error; output is only written on success.
bool GzipCompress(const std::string& input, std::string* output);

// Inflates a gzip or zlib container into output. Returns false on empty
// input, corrupt bytes, a truncated stream, or output beyond
// kMaxDecompressedBytes; output is only written on success.
bool GzipDecompress(const std::string& input, std::string* output);

} // namespace chirp::common
