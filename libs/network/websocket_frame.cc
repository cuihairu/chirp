#include "network/websocket_frame.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

namespace chirp::network {
namespace {

uint64_t ReadU64BE(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | static_cast<uint64_t>(p[i]);
  }
  return v;
}

} // namespace

void WebSocketFrameParser::Append(const uint8_t* data, size_t len) {
  buf_.append(reinterpret_cast<const char*>(data), len);
}

std::optional<WebSocketFrame> WebSocketFrameParser::PopFrame() {
  constexpr size_t kMinHeader = 2;
  constexpr uint64_t kMaxPayload = 16ULL * 1024 * 1024; // scaffolding safety limit

  if (buf_.size() < kMinHeader) {
    return std::nullopt;
  }
  const auto* p = reinterpret_cast<const uint8_t*>(buf_.data());

  const uint8_t b0 = p[0];
  const uint8_t b1 = p[1];

  const bool fin = (b0 & 0x80) != 0;
  const uint8_t opcode = static_cast<uint8_t>(b0 & 0x0F);
  const bool masked = (b1 & 0x80) != 0;

  uint64_t payload_len = static_cast<uint8_t>(b1 & 0x7F);
  size_t off = 2;

  if (payload_len == 126) {
    if (buf_.size() < off + 2) {
      return std::nullopt;
    }
    payload_len = (static_cast<uint64_t>(static_cast<uint8_t>(p[off])) << 8) |
                  static_cast<uint64_t>(static_cast<uint8_t>(p[off + 1]));
    off += 2;
  } else if (payload_len == 127) {
    if (buf_.size() < off + 8) {
      return std::nullopt;
    }
    payload_len = ReadU64BE(p + off);
    off += 8;
  }

  if (payload_len > kMaxPayload) {
    // Too large; drop.
    buf_.clear();
    return std::nullopt;
  }

  uint8_t mask_key[4] = {0, 0, 0, 0};
  if (masked) {
    if (buf_.size() < off + 4) {
      return std::nullopt;
    }
    mask_key[0] = p[off];
    mask_key[1] = p[off + 1];
    mask_key[2] = p[off + 2];
    mask_key[3] = p[off + 3];
    off += 4;
  }

  if (buf_.size() < off + payload_len) {
    return std::nullopt;
  }

  WebSocketFrame f;
  f.fin = fin;
  f.opcode = opcode;
  f.payload.assign(buf_.data() + off, static_cast<size_t>(payload_len));

  if (masked) {
    for (size_t i = 0; i < f.payload.size(); ++i) {
      f.payload[i] = static_cast<char>(static_cast<uint8_t>(f.payload[i]) ^ mask_key[i % 4]);
    }
  }

  buf_.erase(0, off + static_cast<size_t>(payload_len));
  return f;
}

std::string BuildWebSocketFrame(uint8_t opcode, const std::string& payload, bool mask) {
  std::string out;
  const uint64_t len = payload.size();

  out.push_back(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN=1

  uint8_t b1 = mask ? 0x80 : 0x00;
  if (len <= 125) {
    out.push_back(static_cast<char>(b1 | static_cast<uint8_t>(len)));
  } else if (len <= 65535) {
    out.push_back(static_cast<char>(b1 | 126));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));
  } else {
    out.push_back(static_cast<char>(b1 | 127));
    for (int i = 7; i >= 0; --i) {
      out.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
  }

  uint8_t mask_key[4] = {0, 0, 0, 0};
  if (mask) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (int i = 0; i < 4; ++i) {
      mask_key[i] = static_cast<uint8_t>(dist(rng));
      out.push_back(static_cast<char>(mask_key[i]));
    }
  }

  if (!mask) {
    out.append(payload);
    return out;
  }

  out.reserve(out.size() + payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask_key[i % 4]));
  }
  return out;
}

} // namespace chirp::network

