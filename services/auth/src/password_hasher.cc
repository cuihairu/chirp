#include "password_hasher.h"

#include <cstring>
#include <random>
#include <sstream>
#include <string>

#include "logger.h"

// Argon2 header - using libsodium's crypto_pwhash implementation
#include <sodium.h>

namespace chirp::auth {
namespace {

thread_local PasswordHasher::Config g_config = PasswordHasher::Config();

std::string RandomBytes(size_t count) {
  std::string out;
  out.resize(count);
  randombytes_buf(reinterpret_cast<uint8_t*>(out.data()), count);
  return out;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char* kBase64Chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve((len * 4 + 2) / 3);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t value = 0;
    size_t group = std::min(size_t(3), len - i);

    for (size_t j = 0; j < group; j++) {
      value = (value << 8) | data[i + j];
    }

    out.push_back(kBase64Chars[(value >> 18) & 0x3F]);
    out.push_back(kBase64Chars[(value >> 12) & 0x3F]);

    if (group > 1) {
      out.push_back(kBase64Chars[(value >> 6) & 0x3F]);
    } else {
      out.push_back('=');
    }

    if (group > 2) {
      out.push_back(kBase64Chars[value & 0x3F]);
    } else {
      out.push_back('=');
    }
  }

  return out;
}

} // namespace

std::string PasswordHasher::HashPassword(std::string_view password) {
  if (sodium_init() < 0) {
    chirp::common::Logger::Instance().Error("Failed to initialize libsodium");
    return "";
  }

  const auto& cfg = DefaultConfig();

  // Output buffer for the hash (includes salt and parameters)
  std::string out;
  out.resize(crypto_pwhash_argon2id_STRBYTES);

  // Hash using Argon2id (recommended for passwords)
  // This stores the salt and parameters in the output string
  if (crypto_pwhash_str(
          out.data(),
          password.data(),
          password.size(),
          cfg.time_cost,
          cfg.memory_cost) != 0) {
    chirp::common::Logger::Instance().Error("Failed to hash password");
    return "";
  }

  // Remove null terminator if present
  if (!out.empty() && out.back() == '\0') {
    out.pop_back();
  }

  return out;
}

bool PasswordHasher::VerifyPassword(std::string_view password, std::string_view hash) {
  if (sodium_init() < 0) {
    chirp::common::Logger::Instance().Error("Failed to initialize libsodium");
    return false;
  }

  // Convert hash to null-terminated string for libsodium
  std::string hash_str(hash);

  // Verify using Argon2id
  int result = crypto_pwhash_argon2id_str_verify(
      hash_str.c_str(),
      password.data(),
      password.size());

  return result == 0;
}

std::string PasswordHasher::ValidateStrength(std::string_view password) {
  if (password.size() < 8) {
    return "Password must be at least 8 characters";
  }

  bool has_upper = false;
  bool has_lower = false;
  bool has_digit = false;
  bool has_special = false;

  for (char c : password) {
    if (c >= 'A' && c <= 'Z') has_upper = true;
    else if (c >= 'a' && c <= 'z') has_lower = true;
    else if (c >= '0' && c <= '9') has_digit = true;
    else has_special = true;
  }

  int strength_score = (has_upper ? 1 : 0) + (has_lower ? 1 : 0) +
                       (has_digit ? 1 : 0) + (has_special ? 1 : 0);

  if (strength_score < 3) {
    return "Password must contain uppercase, lowercase, numbers, and special characters";
  }

  return "";  // Valid
}

std::string PasswordHasher::GenerateSalt() {
  if (sodium_init() < 0) {
    chirp::common::Logger::Instance().Error("Failed to initialize libsodium");
    return "";
  }

  constexpr size_t kSaltLength = 16;
  std::string salt = RandomBytes(kSaltLength);
  return Base64Encode(reinterpret_cast<const uint8_t*>(salt.data()), salt.length());
}

const PasswordHasher::Config& PasswordHasher::DefaultConfig() {
  return g_config;
}

void PasswordHasher::SetConfig(const Config& config) {
  g_config = config;
}

} // namespace chirp::auth
