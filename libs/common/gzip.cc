#include "gzip.h"

#include <zlib.h>

namespace {

constexpr int kGzipWindowBits = 15 + 16;  // gzip container (RFC 1952)
constexpr int kAutoDetectWindowBits = 15 + 32;  // zlib or gzip container

}  // namespace

namespace chirp::common {

bool GzipCompress(const std::string& input, std::string* output) {
  if (input.empty() || output == nullptr) {
    return false;
  }

  z_stream zs{};
  if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, kGzipWindowBits, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return false;
  }

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  zs.avail_in = static_cast<uInt>(input.size());
  // deflateBound is an upper bound that already includes the gzip
  // header/trailer, so one Z_FINISH call always fits.
  std::string out;
  out.resize(deflateBound(&zs, static_cast<uLong>(input.size())));
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&zs, Z_FINISH);
  deflateEnd(&zs);
  if (rc != Z_STREAM_END) {
    return false;
  }

  out.resize(out.size() - zs.avail_out);
  *output = std::move(out);
  return true;
}

bool GzipDecompress(const std::string& input, std::string* output) {
  if (input.empty() || output == nullptr) {
    return false;
  }

  z_stream zs{};
  if (inflateInit2(&zs, kAutoDetectWindowBits) != Z_OK) {
    return false;
  }

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  zs.avail_in = static_cast<uInt>(input.size());
  std::string out;
  char chunk[16384];
  int rc = Z_OK;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(chunk);
    zs.avail_out = static_cast<uInt>(sizeof(chunk));
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
      inflateEnd(&zs);
      return false;  // corrupt container
    }
    out.append(chunk, sizeof(chunk) - zs.avail_out);
    if (rc == Z_BUF_ERROR && zs.avail_in == 0) {
      inflateEnd(&zs);
      return false;  // truncated: input exhausted before the stream ended
    }
    if (out.size() > kMaxDecompressedBytes) {
      inflateEnd(&zs);
      return false;  // decompression bomb
    }
  } while (rc != Z_STREAM_END);

  inflateEnd(&zs);
  *output = std::move(out);
  return true;
}

}  // namespace chirp::common
