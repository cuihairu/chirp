#pragma once

#include <chrono>

namespace chirp::common {

/// Get current time in milliseconds since epoch
inline int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Get current time in seconds since epoch
inline int64_t NowSec() {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Get high-resolution monotonic time for measurements
inline int64_t SteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace chirp::common
