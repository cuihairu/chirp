#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string_view>

#include "common/base64.h"
#include "common/jwt.h"
#include "common/sha256.h"

namespace chirp::common {
namespace {

std::string ToHex(std::string_view bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out.push_back(kHex[(c >> 4) & 0xF]);
    out.push_back(kHex[c & 0xF]);
  }
  return out;
}

std::string ToHex(const std::array<uint8_t, 32>& bytes) {
  return ToHex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

TEST(Base64UrlTest, EncodeDecodeText) {
  const std::string input = "Hello, Chirp!";
  const std::string encoded = Base64UrlEncode(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  EXPECT_FALSE(encoded.empty());

  std::string decoded;
  EXPECT_TRUE(Base64UrlDecode(encoded, &decoded));
  EXPECT_EQ(input, decoded);
}

TEST(Base64UrlTest, EncodeDecodeBinary) {
  const std::array<uint8_t, 4> bin{{0x00, 0xFF, 0x7F, 0x80}};
  const std::string encoded = Base64UrlEncode(bin.data(), bin.size());
  EXPECT_FALSE(encoded.empty());

  std::string decoded;
  EXPECT_TRUE(Base64UrlDecode(encoded, &decoded));
  ASSERT_EQ(decoded.size(), bin.size());
  EXPECT_EQ(0, std::memcmp(decoded.data(), bin.data(), bin.size()));
}

TEST(Sha256Test, KnownVectors) {
  EXPECT_EQ(ToHex(Sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(ToHex(Sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(HmacSha256Test, KnownVector) {
  const auto mac = HmacSha256("key", "The quick brown fox jumps over the lazy dog");
  EXPECT_EQ(ToHex(mac), "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST(JwtTest, HS256SignVerify) {
  const std::string secret = "test_secret";
  const std::string subject = "user123";
  const int64_t iat = 1700000000;

  const std::string token = JwtSignHS256(subject, iat, secret);
  EXPECT_FALSE(token.empty());

  JwtClaims parsed;
  std::string err;
  EXPECT_TRUE(JwtVerifyHS256(token, secret, &parsed, &err));
  EXPECT_TRUE(err.empty());
  EXPECT_EQ(parsed.subject, subject);
  EXPECT_EQ(parsed.issued_at, iat);
}

TEST(JwtTest, InvalidTokenRejected) {
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256("not.a.valid.token", "secret", &parsed, &err));
  EXPECT_FALSE(err.empty());
}

TEST(JwtTest, WrongSecretRejected) {
  const std::string token = JwtSignHS256("user123", 1, "secret1");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "secret2", &parsed, &err));
}

} // namespace
} // namespace chirp::common
