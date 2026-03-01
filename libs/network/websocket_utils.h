#pragma once

#include <string>

namespace chirp::network {

/// Build a WebSocket handshake HTTP request
/// @param host Target host (e.g., "localhost" or "example.com")
/// @param port Target port (e.g., 8080)
/// @param path WebSocket path (default: "/")
/// @return Complete HTTP handshake request as string
inline std::string BuildWebSocketHandshake(
    const std::string& host,
    uint16_t port,
    const std::string& path = "/") {

  return "GET " + path + " HTTP/1.1\r\n"
         "Host: " + host + ":" + std::to_string(port) + "\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
         "Sec-WebSocket-Version: 13\r\n"
         "\r\n";
}

/// Check if HTTP response indicates successful WebSocket upgrade
/// @param response HTTP response string
/// @return true if response contains "101 Switching Protocols"
inline bool IsWebSocketUpgradeSuccessful(const std::string& response) {
  return response.find("101 Switching Protocols") != std::string::npos;
}

} // namespace chirp::network
