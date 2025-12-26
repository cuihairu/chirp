#include "common/base64.h"

#include <array>

namespace chirp::common {
namespace {

constexpr char kB64UrlTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::array<int8_t, 256> MakeDecodeTable() {
  std::array<int8_t, 256> t{};
  t.fill(-1);
  for (int i = 0; i < 64; ++i) {
    t[static_cast<uint8_t>(kB64UrlTable[i])] = static_cast<int8_t>(i);
  }
  t[static_cast<uint8_t>('=')] = 0;
  return t;
}

const std::array<int8_t, 256>& DecodeTable() {
  static const auto t = MakeDecodeTable();
  return t;
}

} // namespace

std::string Base64UrlEncode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);

  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                       static_cast<uint32_t>(data[i + 2]);
    out.push_back(kB64UrlTable[(n >> 18) & 0x3F]);
    out.push_back(kB64UrlTable[(n >> 12) & 0x3F]);
    out.push_back(kB64UrlTable[(n >> 6) & 0x3F]);
    out.push_back(kB64UrlTable[n & 0x3F]);
    i += 3;
  }

  const size_t rem = len - i;
  if (rem == 1) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16);
    out.push_back(kB64UrlTable[(n >> 18) & 0x3F]);
    out.push_back(kB64UrlTable[(n >> 12) & 0x3F]);
  } else if (rem == 2) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kB64UrlTable[(n >> 18) & 0x3F]);
    out.push_back(kB64UrlTable[(n >> 12) & 0x3F]);
    out.push_back(kB64UrlTable[(n >> 6) & 0x3F]);
  }

  return out;
}

bool Base64UrlDecode(std::string_view in, std::string* out) {
  if (!out) {
    return false;
  }

  // Copy and pad to multiple of 4.
  std::string s(in);
  while ((s.size() % 4) != 0) {
    s.push_back('=');
  }

  const auto& t = DecodeTable();
  out->clear();
  out->reserve((s.size() / 4) * 3);

  for (size_t i = 0; i < s.size(); i += 4) {
    const int8_t a = t[static_cast<uint8_t>(s[i])];
    const int8_t b = t[static_cast<uint8_t>(s[i + 1])];
    const int8_t c = t[static_cast<uint8_t>(s[i + 2])];
    const int8_t d = t[static_cast<uint8_t>(s[i + 3])];
    if (a < 0 || b < 0 || c < 0 || d < 0) {
      return false;
    }

    const uint32_t n = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
                       (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);

    out->push_back(static_cast<char>((n >> 16) & 0xFF));
    if (s[i + 2] != '=') {
      out->push_back(static_cast<char>((n >> 8) & 0xFF));
    }
    if (s[i + 3] != '=') {
      out->push_back(static_cast<char>(n & 0xFF));
    }
  }
  return true;
}

} // namespace chirp::common

