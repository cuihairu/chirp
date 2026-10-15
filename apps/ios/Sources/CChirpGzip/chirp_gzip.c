// zlib-backed gzip reader; see include/chirp_gzip.h for the contract.
#include "include/chirp_gzip.h"

#include <string.h>
#include <zlib.h>

// windowBits 15+16 = gzip container only; 15+32 = accept zlib or gzip, so a
// stray zlib-wrapped payload still reads (the C++ core's inflate primitive
// uses the same auto-detecting form).
#define CHIRP_GZIP_WINDOW_BITS (15 + 32)

int chirp_gzip_inflate(const unsigned char *container, size_t container_len,
                       unsigned char *buf, size_t buf_capacity,
                       size_t *out_len) {
    if (container == NULL || buf == NULL || out_len == NULL) {
        return CHIRP_GZIP_INVALID;
    }
    *out_len = 0;
    if (container_len == 0 || buf_capacity == 0) {
        return CHIRP_GZIP_INVALID;
    }

    z_stream stream;
    // zlib reads zalloc/zfree/opaque and treats a NULL zalloc as "use the
    // library's defaults", so a zeroed struct is the documented init.
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, CHIRP_GZIP_WINDOW_BITS) != Z_OK) {
        return CHIRP_GZIP_CORRUPT;
    }

    stream.next_in = (Bytef *)container;
    stream.avail_in = (uInt)container_len;
    stream.next_out = buf;
    stream.avail_out = (uInt)buf_capacity;

    int rc = inflate(&stream, Z_NO_FLUSH);
    if (rc != Z_STREAM_END && rc == Z_OK && stream.avail_out == 0 && stream.avail_in > 0) {
        // The body wants more room than the caller offered. Report the partial
        // length so the retry can be sized without a second inflate pass.
        *out_len = buf_capacity;
        inflateEnd(&stream);
        return CHIRP_GZIP_AGAIN;
    }
    if (rc != Z_STREAM_END) {
        // Z_DATA_ERROR / Z_MEM_ERROR / Z_BUF_ERROR all read as one failure:
        // the container did not decode to a complete stream.
        inflateEnd(&stream);
        return CHIRP_GZIP_CORRUPT;
    }

    *out_len = buf_capacity - stream.avail_out;
    inflateEnd(&stream);
    return CHIRP_GZIP_OK;
}