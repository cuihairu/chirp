#pragma once

#include <cstdint>
#include <string>

namespace chirp::network {

// Computes "Sec-WebSocket-Accept" value for a given "Sec-WebSocket-Key".
std::string ComputeWebSocketAccept(const std::string& sec_websocket_key);

// Returns true if `s` starts with `prefix` (case-insensitive ASCII).
bool IStartsWith(const std::string& s, const std::string& prefix);

// Trims ASCII whitespace (space/tab/CR/LF) from both ends.
std::string TrimAsciiWhitespace(std::string s);

} // namespace chirp::network

