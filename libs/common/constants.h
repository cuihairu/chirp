#pragma once

#include <cstdint>

namespace chirp::common::constants {

// Device identifiers
namespace device {
  constexpr const char* CLI_CLIENT = "cli_client";
  constexpr const char* TEST_CLIENT = "test_client";
  constexpr const char* WEB_CLIENT = "web_client";
  constexpr const char* GAME_CLIENT = "game_client";
  constexpr const char* MOBILE_CLIENT = "mobile_client";
} // namespace device

// Platform identifiers
namespace platform {
  constexpr const char* PC = "pc";
  constexpr const char* MOBILE = "mobile";
  constexpr const char* WEB = "web";
  constexpr const char* IOS = "ios";
  constexpr const char* ANDROID = "android";
} // namespace platform

// Default hosts for local development
namespace host {
  constexpr const char* LOCALHOST = "localhost";
  constexpr const char* GATEWAY = "localhost";
  constexpr const char* AUTH = "localhost";
  constexpr const char* CHAT = "localhost";
  constexpr const char* SOCIAL = "localhost";
  constexpr const char* VOICE = "localhost";
} // namespace host

// Default ports
namespace port {
  constexpr uint16_t GATEWAY_TCP = 5000;
  constexpr uint16_t GATEWAY_WS = 5001;
  constexpr uint16_t AUTH = 6000;
  constexpr uint16_t CHAT_TCP = 7000;
  constexpr uint16_t CHAT_WS = 7001;
  constexpr uint16_t SOCIAL_TCP = 8000;
  constexpr uint16_t SOCIAL_WS = 8001;
  constexpr uint16_t VOICE_TCP = 9000;
  constexpr uint16_t VOICE_WS = 9001;
  constexpr uint16_t REDIS = 6379;
  constexpr uint16_t MYSQL = 3306;
} // namespace port

// Timeouts (milliseconds)
namespace timeout {
  constexpr int64_t DEFAULT = 5000;        // 5 seconds
  constexpr int64_t CONNECT = 10000;       // 10 seconds
  constexpr int64_t SHORT = 2000;          // 2 seconds
  constexpr int64_t LONG = 30000;          // 30 seconds
  constexpr int64_t HEARTBEAT = 30000;     // 30 seconds
} // namespace timeout

// Buffer sizes
namespace buffer {
  constexpr size_t DEFAULT = 4096;        // 4 KB
  constexpr size_t LARGE = 65536;         // 64 KB
  constexpr size_t MAX_PACKET = 1048576;   // 1 MB
} // namespace buffer

} // namespace chirp::common::constants
