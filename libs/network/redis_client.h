#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <asio.hpp>

namespace chirp::network {

/// @brief Redis 同步客户端（用于命令操作）
class RedisClient {
public:
  RedisClient(std::string host, uint16_t port);

  // 基本命令
  std::optional<std::string> Get(const std::string& key);
  bool SetEx(const std::string& key, const std::string& value, int ttl_seconds);
  bool Del(const std::string& key);

  // Pub/Sub 命令
  bool Publish(const std::string& channel, const std::string& message);

  // List 命令
  bool RPush(const std::string& key, const std::string& value);
  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop);

  // 过期命令
  bool Expire(const std::string& key, int ttl_seconds);

  // Keys 命令
  std::vector<std::string> Keys(const std::string& pattern);

private:
  std::string host_;
  uint16_t port_;
};

/// @brief Redis 订阅者（支持多频道订阅）
class RedisSubscriber {
public:
  using MessageCallback = std::function<void(const std::string& channel, const std::string& payload)>;
  using ErrorCallback = std::function<void(const std::string& error)>;
  using ConnectCallback = std::function<void()>;

  RedisSubscriber(std::string host, uint16_t port);
  ~RedisSubscriber();

  RedisSubscriber(const RedisSubscriber&) = delete;
  RedisSubscriber& operator=(const RedisSubscriber&) = delete;

  /// @brief 设置消息回调
  void SetMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

  /// @brief 设置错误回调
  void SetErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }

  /// @brief 设置连接回调
  void SetConnectCallback(ConnectCallback cb) { connect_cb_ = std::move(cb); }

  /// @brief 订阅频道
  bool Subscribe(const std::string& channel);

  /// @brief 取消订阅频道
  bool Unsubscribe(const std::string& channel);

  /// @brief 启动订阅者（需要先设置回调）
  void Start();

  /// @brief 停止订阅者
  void Stop();

  /// @brief 检查是否已连接
  bool IsConnected() const { return connected_; }

private:
  void Run();
  bool SendCommand(const std::string& cmd);
  void ProcessLine(const std::string& line);

  std::string host_;
  uint16_t port_;

  MessageCallback msg_cb_;
  ErrorCallback error_cb_;
  ConnectCallback connect_cb_;

  std::atomic<bool> stop_{false};
  std::atomic<bool> connected_{false};

  std::mutex sock_mu_;
  std::unique_ptr<asio::io_context> io_;
  asio::ip::tcp::socket* socket_ptr_{nullptr};
  std::unique_ptr<asio::ip::tcp::socket> socket_;
  std::thread th_;
};

} // namespace chirp::network
