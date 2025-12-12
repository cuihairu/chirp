#include "network/length_prefixed_framer.h"

#include "network/byte_order.h"

namespace chirp::network {

void LengthPrefixedFramer::Append(const uint8_t* data, size_t len) {
  buf_.append(reinterpret_cast<const char*>(data), len);
}

std::optional<std::string> LengthPrefixedFramer::PopFrame() {
  constexpr size_t kLenBytes = 4;
  if (buf_.size() < kLenBytes) {
    return std::nullopt;
  }
  const auto* p = reinterpret_cast<const uint8_t*>(buf_.data());
  uint32_t n = ReadU32BE(p);
  if (buf_.size() < kLenBytes + n) {
    return std::nullopt;
  }
  std::string payload = buf_.substr(kLenBytes, n);
  buf_.erase(0, kLenBytes + n);
  return payload;
}

} // namespace chirp::network

