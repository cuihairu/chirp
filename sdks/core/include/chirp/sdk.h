#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>

namespace chirp {
namespace sdk {

// SDK 回调类型
using LoginCallback = std::function<void(const std::error_code& ec, const std::string& user_id)>;
using MessageCallback = std::function<void(const std::string& sender, const std::string& content)>;
using DisconnectCallback = std::function<void(const std::error_code& ec)>;
using KickCallback = std::function<void(const std::string& reason)>;

// SDK 配置
struct ChatConfig {
  std::string gateway_host = "localhost";
  uint16_t gateway_port = 5000;
  uint16_t gateway_ws_port = 5001;

  bool enable_websocket = false;  // 使用 WebSocket 而不是 TCP
  int heartbeat_interval_seconds = 30;
  int reconnect_interval_seconds = 5;
  int max_reconnect_attempts = -1;  // -1 表示无限重连
};

// SDK 状态
enum class ConnectionState {
  Disconnected,
  Connecting,
  Connected,
  LoggedIn
};

// 简单的字符串错误码
enum class ChatError {
  OK = 0,
  NotConnected = 1,
  AlreadyConnected = 2,
  LoginFailed = 3,
  SendFailed = 4,
  InvalidParam = 5,
  Timeout = 6
};

std::error_code make_error_code(ChatError e);

} // namespace sdk
} // namespace chirp

namespace std {
  template<> struct is_error_code_enum<chirp::sdk::ChatError> : true_type {};
}
