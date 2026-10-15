// zlib-backed gzip reader for the history-readback compression shape
// (game_chat_features P2 消息压缩). Swift ships no stdlib inflate and the
// Apple Compression framework is Darwin-only, so the primitive is a C shim
// over zlib — the same library the C++ core's libs/common/gzip.cc uses. Both
// platforms (iOS SDK, Linux toolchain) ship zlib headers and libz.
//
// The shim inflates into a caller-provided buffer so the decompressed-size cap
// stays structural (the caller owns the buffer) instead of being a check
// someone can forget. A body bigger than the buffer reports CHIRP_GZIP_AGAIN
// so the caller can grow and retry rather than allocate the whole cap up
// front on every history readback.
#ifndef CHIRP_GZIP_H
#define CHIRP_GZIP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Container decoded; *out_len holds the inflated length.
#define CHIRP_GZIP_OK 0
/// Bad arguments or empty input.
#define CHIRP_GZIP_INVALID -1
/// The inflated body did not fit in `buf_capacity` (grow and retry).
#define CHIRP_GZIP_AGAIN -2
/// Corrupt or truncated container (zlib never reached the stream end).
#define CHIRP_GZIP_CORRUPT -3

// Inflates a gzip (RFC 1952) container into `buf` (at least `buf_capacity`
// bytes). On CHIRP_GZIP_OK stores the inflated length in `*out_len`; on
// CHIRP_GZIP_AGAIN `*out_len` holds how much of the body did fit, so the
// caller can size the next buffer.
int chirp_gzip_inflate(const unsigned char *container, size_t container_len,
                       unsigned char *buf, size_t buf_capacity,
                       size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif  // CHIRP_GZIP_H