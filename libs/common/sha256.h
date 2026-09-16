#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The project's single SHA-256 / HMAC-SHA256 implementation (used by JWT
// HS256 and token digests). libsodium is NOT a second hash implementation:
// auth uses it only for Argon2id password hashing (crypto_pwhash) and
// random token bytes (password_hasher / token_generator).

namespace chirp::common {

std::array<uint8_t, 32> Sha256(std::string_view data);
std::array<uint8_t, 32> HmacSha256(std::string_view key, std::string_view msg);

// Constant-time byte compare.
bool SecureEqual(std::string_view a, std::string_view b);

} // namespace chirp::common

