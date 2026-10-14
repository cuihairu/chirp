// gzip round-trip and guard tests (libs/common/gzip.{h,cc}) - the
// history-readback compression primitive (game_chat_features P2 消息压缩):
// gzip container on compress, auto-detect on decompress, plus the empty /
// corrupt / truncated / bomb-cap guards.

#include <gtest/gtest.h>

#include <string>

#include <zlib.h>

#include "gzip.h"

namespace {

using chirp::common::GzipCompress;
using chirp::common::GzipDecompress;

TEST(GzipTest, RoundTripPreservesBytes) {
  const std::string input = "chirp history payload \x01\x02 with 中文 and repeats";
  std::string compressed;
  ASSERT_TRUE(GzipCompress(input, &compressed));
  std::string restored;
  ASSERT_TRUE(GzipDecompress(compressed, &restored));
  EXPECT_EQ(restored, input);
}

TEST(GzipTest, CompressedOutputCarriesGzipMagic) {
  // RFC 1952: the container starts with 1f 8b, so every runtime's gzip
  // reader (DecompressionStream('gzip'), GZipStream, compress/gzip,
  // GZIPInputStream) accepts it unconfigured.
  std::string compressed;
  ASSERT_TRUE(GzipCompress("payload", &compressed));
  ASSERT_GE(compressed.size(), 2u);
  EXPECT_EQ(static_cast<unsigned char>(compressed[0]), 0x1f);
  EXPECT_EQ(static_cast<unsigned char>(compressed[1]), 0x8b);
}

TEST(GzipTest, CompressShrinksHighlyRepetitiveInput) {
  const std::string input(100000, 'a');
  std::string compressed;
  ASSERT_TRUE(GzipCompress(input, &compressed));
  EXPECT_LT(compressed.size(), input.size() / 10);
}

TEST(GzipTest, EmptyInputFailsCompressAndDecompress) {
  std::string out;
  EXPECT_FALSE(GzipCompress("", &out));
  EXPECT_FALSE(GzipDecompress("", &out));
}

TEST(GzipTest, NullOutputPointerFails) {
  EXPECT_FALSE(GzipCompress("x", nullptr));
  EXPECT_FALSE(GzipDecompress("x", nullptr));
}

TEST(GzipTest, CorruptContainerFailsDecompress) {
  std::string compressed;
  ASSERT_TRUE(GzipCompress(std::string(2000, 'y'), &compressed));
  ASSERT_FALSE(compressed.empty());
  compressed[compressed.size() / 2] ^= 0xff;
  std::string out;
  EXPECT_FALSE(GzipDecompress(compressed, &out));
}

TEST(GzipTest, CorruptHeaderFailsDecompressImmediately) {
  // Flipping the magic byte kills the container header: inflate
  // auto-detection rejects it outright (Z_DATA_ERROR), the corrupt-container
  // guard, before any payload bytes are read.
  std::string compressed;
  ASSERT_TRUE(GzipCompress(std::string(2000, 'y'), &compressed));
  compressed[0] = 'X';
  std::string out;
  EXPECT_FALSE(GzipDecompress(compressed, &out));
}

TEST(GzipTest, TruncatedStreamFailsDecompress) {
  std::string compressed;
  ASSERT_TRUE(GzipCompress(std::string(10000, 'x'), &compressed));
  std::string out;
  EXPECT_FALSE(GzipDecompress(compressed.substr(0, compressed.size() / 2), &out));
}

TEST(GzipTest, DecompressionBeyondCapFails) {
  // 17 MiB of zeros compresses to a few KB but inflates past the 16 MiB
  // bomb cap (kMaxDecompressedBytes).
  const std::string big(static_cast<size_t>(17) * 1024 * 1024, '\0');
  std::string compressed;
  ASSERT_TRUE(GzipCompress(big, &compressed));
  std::string out;
  EXPECT_FALSE(GzipDecompress(compressed, &out));
}

TEST(GzipTest, DecompressAcceptsZlibContainerToo) {
  // inflate runs with auto-detect (windowBits 15+32): a zlib-wrapped
  // container (windowBits 15) decompresses through the same entry point.
  const std::string input = "zlib-wrapped payload";
  z_stream zs{};
  ASSERT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8,
                         Z_DEFAULT_STRATEGY),
            Z_OK);
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  zs.avail_in = static_cast<uInt>(input.size());
  std::string wrapped(deflateBound(&zs, static_cast<uLong>(input.size())), '\0');
  zs.next_out = reinterpret_cast<Bytef*>(wrapped.data());
  zs.avail_out = static_cast<uInt>(wrapped.size());
  ASSERT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  deflateEnd(&zs);
  wrapped.resize(wrapped.size() - zs.avail_out);

  std::string restored;
  ASSERT_TRUE(GzipDecompress(wrapped, &restored));
  EXPECT_EQ(restored, input);
}

}  // namespace
