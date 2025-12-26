#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace chirp::common {

// URL-safe Base64 without padding, per RFC 7515/7519 usage.
std::string Base64UrlEncode(const uint8_t* data, size_t len);

// Decodes URL-safe Base64 (with or without padding). Returns false on invalid input.
bool Base64UrlDecode(std::string_view in, std::string* out);

} // namespace chirp::common

