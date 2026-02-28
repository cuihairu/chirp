#ifndef CHIRP_CORE_CONFIG_H_
#define CHIRP_CORE_CONFIG_H_

#include <cstdint>
#include <string>

namespace chirp {
namespace core {

// Connection configuration
struct ConnectionConfig {
  std::string gateway_host = "localhost";
  uint16_t gateway_port = 5000;
  uint16_t gateway_ws_port = 5001;

  std::string auth_host = "localhost";
  uint16_t auth_port = 6000;

  std::string chat_host = "localhost";
  uint16_t chat_port = 7000;
  uint16_t chat_ws_port = 7001;

  std::string social_host = "localhost";
  uint16_t social_port = 8000;
  uint16_t social_ws_port = 8001;

  std::string voice_host = "localhost";
  uint16_t voice_port = 9000;
  uint16_t voice_ws_port = 9001;

  // Connection settings
  bool use_websocket = true;
  int32_t connect_timeout_ms = 10000;
  int32_t receive_timeout_ms = 30000;

  // Reconnect settings
  bool auto_reconnect = true;
  int32_t max_reconnect_attempts = 5;
  int32_t reconnect_delay_ms = 2000;
};

// SDK configuration
struct Config {
  // App identification
  std::string app_id;
  std::string app_version = "1.0.0";

  // User credentials (for login)
  std::string user_id;
  std::string device_id;
  std::string platform;  // "ios", "android", "windows", "macos", "linux", "web"

  // Connection settings
  ConnectionConfig connection;

  // Logging
  bool enable_logging = true;
  std::string log_level = "info";  // "debug", "info", "warn", "error"

  // Thread pool size (0 = auto)
  uint32_t thread_pool_size = 0;

  // Event callback thread pool size
  uint32_t event_thread_pool_size = 2;
};

} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_CONFIG_H_
