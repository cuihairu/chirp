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

// 通用请求回调:resp 为完整响应 Packet(body 由调用方按 resp_msg_id Parse);
// ec 非 OK 时 resp 无意义。所有回调在 SDK 内部 io 线程触发,引擎适配层
// (Unreal/桌面)负责派发回游戏线程。
using ResponseCallback = std::function<void(const std::error_code& ec, const std::string& body)>;
using NotifyCallback = std::function<void(const std::string& body)>;

// OnNotify 返回的订阅句柄;0 表示无效(未连接时订阅仍返回句柄,连接后生效)。
using NotifyHandle = uint64_t;

// SDK 配置
struct ChatConfig {
  std::string gateway_host = "localhost";
  uint16_t gateway_port = 5000;
  uint16_t gateway_ws_port = 5001;

  // 仅支持 TCP 长连接(length-prefixed Packet 帧,与 WebSocket 消息内的帧
  // 协议一致)。WS 外壳等 app_gateway 聚合边缘定案后再对齐;置 true 仍是
  // fast-fail(连接前即回调 Disconnect)。
  bool enable_websocket = false;
  int heartbeat_interval_seconds = 25;
  // 连续 N 个心跳未收到 pong 回声即判定连接死亡并进入自动重连。
  int max_missed_pongs = 2;
  // 运行中断线的自动重连:指数退避 500ms 起、每翻倍、上限 15s、±20% 抖动;
  // KICK 是终态,不自动重连。
  int max_reconnect_attempts = -1;  // -1 表示无限重连
  // 单个请求的响应超时;超时回调 Timeout,迟到的响应被无害丢弃。
  int request_timeout_ms = 10000;

  // 已弃用:重连节奏由上面的固定退避策略决定,该字段不再读取。
  int reconnect_interval_seconds = 5;
};

// SDK 状态
enum class ConnectionState {
  Disconnected,
  Connecting,
  Connected,
  LoggedIn,
  // 运行中断线后等待退避重连;Disconnect() 或新的 Connect() 会离开该状态。
  WaitingReconnect,
  // 收到 KICK_NOTIFY:终态,不自动重连,需重新 Connect+Login。
  Kicked,
};

// 简单的字符串错误码
enum class ChatError {
  OK = 0,
  NotConnected = 1,
  AlreadyConnected = 2,
  LoginFailed = 3,
  SendFailed = 4,
  InvalidParam = 5,
  Timeout = 6,
  // 连接断开,请求被 flush(随后可能自动重连)。
  Closed = 7,
  // 会话被顶号(KICK_NOTIFY),请求被 flush,且不再重连。
  Kicked = 8,
  // 响应帧无法按预期消息类型解析(协议层异常;正常服务端不会发生)。
  BadResponse = 9,
};

std::error_code make_error_code(ChatError e);

} // namespace sdk
} // namespace chirp

namespace std {
  template<> struct is_error_code_enum<chirp::sdk::ChatError> : true_type {};
}
