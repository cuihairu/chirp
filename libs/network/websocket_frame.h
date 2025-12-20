#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace chirp::network {

struct WebSocketFrame {
  uint8_t opcode{0};
  bool fin{true};
  std::string payload;
};

class WebSocketFrameParser {
public:
  void Append(const uint8_t* data, size_t len);
  std::optional<WebSocketFrame> PopFrame();
  void Clear() { buf_.clear(); }

private:
  std::string buf_;
};

std::string BuildWebSocketFrame(uint8_t opcode, const std::string& payload, bool mask);

} // namespace chirp::network
