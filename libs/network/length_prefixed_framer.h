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
  // Declared-length cap, the same 16MB as the websocket payload limit: a
  // corrupt or hostile 4-byte header must not pin gigabytes of buffer.
  static constexpr size_t kMaxFrame = 16ULL * 1024 * 1024;

  // Appends raw bytes into the internal buffer.
  void Append(const uint8_t* data, size_t len);

  // Pops the next full payload (without length prefix). Returns nullopt if
  // incomplete. On a declared length over kMaxFrame it drops the buffer and
  // latches overflow(); the caller is expected to close the connection.
  std::optional<std::string> PopFrame();

  void Clear() { buf_.clear(); }
  size_t BufferedBytes() const { return buf_.size(); }
  bool overflow() const { return overflow_; }

private:
  std::string buf_;
  bool overflow_ = false;
};

} // namespace chirp::network

