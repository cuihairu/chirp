#pragma once

// Internal reconnect-backoff helper. Lives in a header so the sdk_core test
// target can exercise the doubling schedule and the cap deterministically —
// walking the real schedule through live reconnects would need tens of
// seconds of wall time. Not part of the stable SDK API.

#include <cstdint>
#include <cstdlib>

namespace chirp::sdk::internal {

// 与 web/mobile/unity 客户端一致的退避参数:500ms 起步、翻倍、15s 封顶、
// ±20% 抖动。
constexpr int64_t kReconnectBaseMs = 500;
constexpr int64_t kReconnectMaxMs = 15000;

inline int64_t BackoffDelayMs(int attempt) {
  int64_t delay = kReconnectBaseMs;
  for (int i = 0; i < attempt && delay < kReconnectMaxMs; ++i) {
    delay *= 2;
  }
  if (delay > kReconnectMaxMs) {
    delay = kReconnectMaxMs;
  }
  // ±20% jitter:抖动幅度随延迟缩放,重连风暴不会整点对齐。
  const auto mod = static_cast<int64_t>(delay / 5);
  if (mod > 0) {
    const auto jitter = static_cast<int64_t>(std::rand() % (2 * mod + 1)) - mod;
    delay += jitter;
  }
  return delay;
}

}  // namespace chirp::sdk::internal
