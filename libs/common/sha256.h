#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace chirp::common {

std::array<uint8_t, 32> Sha256(std::string_view data);
std::array<uint8_t, 32> HmacSha256(std::string_view key, std::string_view msg);

// Constant-time byte compare.
bool SecureEqual(std::string_view a, std::string_view b);

} // namespace chirp::common

