#include "token_generator.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>

#include <sodium.h>

#include "jwt.h"
#include "sha256.h"

namespace chirp::auth {
namespace {

std::string ToHex(const uint8_t* data, size_t len) {
  static const char* kHexChars = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    out.push_back(kHexChars[(b >> 4) & 0x0F]);
    out.push_back(kHexChars[b & 0x0F]);
  }
  return out;
}

int64_t NowSeconds() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string HashCombined(std::string_view a, std::string_view b) {
  std::string combined = std::string(a) + "|" + std::string(b);
  auto hash = chirp::common::Sha256(combined);
  return ToHex(hash.data(), hash.size());
}

} // namespace

std::string TokenGenerator::GenerateTokenId(size_t bytes) {
  if (sodium_init() < 0) {
    // Fallback to std::random if libsodium not available
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
      uint8_t b = static_cast<uint8_t>(dist(gen));
      out.push_back("0123456789abcdef"[(b >> 4) & 0x0F]);
      out.push_back("0123456789abcdef"[b & 0x0F]);
    }
    return out;
  }

  std::string out;
  out.resize(bytes);
  randombytes_buf(reinterpret_cast<uint8_t*>(out.data()), bytes);
  return ToHex(reinterpret_cast<const uint8_t*>(out.data()), bytes);
}

std::string TokenGenerator::GenerateSessionId(std::string_view user_id) {
  // Combine user_id with timestamp and randomness
  int64_t now = NowSeconds();
  std::string entropy = GenerateTokenId(8);
  return "sess_" + HashCombined(user_id, std::to_string(now) + entropy);
}

std::string TokenGenerator::GenerateRefreshToken() {
  return "rt_" + GenerateTokenId(32);
}

std::string TokenGenerator::GenerateResetToken() {
  return "reset_" + GenerateTokenId(16);
}

std::string TokenGenerator::HashDeviceId(std::string_view device_id, std::string_view platform) {
  return HashCombined(device_id, platform);
}

std::string TokenGenerator::HashToken(std::string_view token) {
  auto hash = chirp::common::Sha256(token);
  return ToHex(hash.data(), hash.size());
}

bool TokenGenerator::VerifyTokenHash(std::string_view token, std::string_view token_hash) {
  std::string computed_hash = HashToken(token);
  return computed_hash == token_hash;
}

std::string TokenGenerator::GenerateAccessToken(std::string_view user_id,
                                                std::string_view secret,
                                                int64_t expires_in_seconds) {
  int64_t now = NowSeconds();

  // Use JWT library to create token with expiration
  // First create a basic JWT, then add expiration claim manually
  chirp::common::JwtClaims claims;
  claims.subject = std::string(user_id);
  claims.issued_at = now;

  std::string jwt = chirp::common::JwtSignHS256(claims.subject, claims.issued_at, secret);

  // Note: Our current JWT implementation only supports sub and iat
  // For production, consider adding exp claim support to jwt.cc
  return jwt;
}

std::string TokenGenerator::GenerateUserId(std::string_view identifier) {
  // Create a consistent user_id from identifier
  auto hash = chirp::common::Sha256(identifier);

  // Use first 16 bytes (128 bits) for user_id
  return "user_" + ToHex(hash.data(), 16);
}

} // namespace chirp::auth
