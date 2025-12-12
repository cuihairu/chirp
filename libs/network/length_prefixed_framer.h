#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace chirp::network {

// Simple length-prefixed framing:
//   [u32_be length][payload bytes...]
class LengthPrefixedFramer {
public:
  // Appends raw bytes into the internal buffer.
  void Append(const uint8_t* data, size_t len);

  // Pops the next full payload (without length prefix). Returns nullopt if incomplete.
  std::optional<std::string> PopFrame();

  void Clear() { buf_.clear(); }
  size_t BufferedBytes() const { return buf_.size(); }

private:
  std::string buf_;
};

} // namespace chirp::network

