#include "network/websocket_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace chirp::network {
namespace {

constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

uint32_t Rol32(uint32_t v, uint32_t bits) { return (v << bits) | (v >> (32 - bits)); }

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len) {
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  // Pre-processing: padding
  const uint64_t bit_len = static_cast<uint64_t>(len) * 8;
  std::string msg(reinterpret_cast<const char*>(data), len);
  msg.push_back(static_cast<char>(0x80));
  while ((msg.size() % 64) != 56) {
    msg.push_back('\0');
  }
  for (int i = 7; i >= 0; --i) {
    msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));
  }

  const uint8_t* p = reinterpret_cast<const uint8_t*>(msg.data());
  const size_t blocks = msg.size() / 64;

  for (size_t b = 0; b < blocks; ++b) {
    uint32_t w[80];
    const uint8_t* chunk = p + b * 64;
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
             (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) | (static_cast<uint32_t>(chunk[i * 4 + 3]));
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = Rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h0;
    uint32_t b2 = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;

    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b2 & c) | ((~b2) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b2 ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b2 & c) | (b2 & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b2 ^ c ^ d;
        k = 0xCA62C1D6;
      }

      uint32_t temp = Rol32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol32(b2, 30);
      b2 = a;
      a = temp;
    }

    h0 += a;
    h1 += b2;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<uint8_t, 20> out{};
  const uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; ++i) {
    out[i * 4] = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
    out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
    out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
    out[i * 4 + 3] = static_cast<uint8_t>((hs[i]) & 0xFF);
  }
  return out;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
  static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);

  size_t i = 0;
  while (i + 3 <= len) {
    uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                 static_cast<uint32_t>(data[i + 2]);
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.push_back(kTable[(n >> 6) & 0x3F]);
    out.push_back(kTable[n & 0x3F]);
    i += 3;
  }

  const size_t rem = len - i;
  if (rem == 1) {
    uint32_t n = (static_cast<uint32_t>(data[i]) << 16);
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.push_back(kTable[(n >> 6) & 0x3F]);
    out.push_back('=');
  }

  return out;
}

} // namespace

std::string ComputeWebSocketAccept(const std::string& sec_websocket_key) {
  std::string in = sec_websocket_key;
  in += kWebSocketGuid;
  auto digest = Sha1(reinterpret_cast<const uint8_t*>(in.data()), in.size());
  return Base64Encode(digest.data(), digest.size());
}

bool IStartsWith(const std::string& s, const std::string& prefix) {
  if (s.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = s[i];
    char b = prefix[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<char>(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

std::string TrimAsciiWhitespace(std::string s) {
  auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  size_t start = 0;
  while (start < s.size() && is_ws(s[start])) {
    start++;
  }
  size_t end = s.size();
  while (end > start && is_ws(s[end - 1])) {
    end--;
  }
  return s.substr(start, end - start);
}

} // namespace chirp::network

