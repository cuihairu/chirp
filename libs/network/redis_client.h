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

/// @brief Synchronous Redis client for command operations.
class RedisClient {
public:
  RedisClient(std::string host, uint16_t port);

  // Basic commands
  std::optional<std::string> Get(const std::string& key);
  bool SetEx(const std::string& key, const std::string& value, int ttl_seconds);
  bool Del(const std::string& key);

  // Pub/Sub commands
  bool Publish(const std::string& channel, const std::string& message);

  // List commands
  bool RPush(const std::string& key, const std::string& value);
  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop);

  // Expiration commands
  bool Expire(const std::string& key, int ttl_seconds);

  // Keys command
  std::vector<std::string> Keys(const std::string& pattern);

private:
  std::string host_;
  uint16_t port_;
};

/// @brief Redis subscriber with multi-channel support.
class RedisSubscriber {
public:
  using MessageCallback = std::function<void(const std::string& channel, const std::string& payload)>;
  using ErrorCallback = std::function<void(const std::string& error)>;
  using ConnectCallback = std::function<void()>;

  RedisSubscriber(std::string host, uint16_t port);
  ~RedisSubscriber();

  RedisSubscriber(const RedisSubscriber&) = delete;
  RedisSubscriber& operator=(const RedisSubscriber&) = delete;

  /// @brief Set the message callback.
  void SetMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

  /// @brief Set the error callback.
  void SetErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }

  /// @brief Set the connect callback.
  void SetConnectCallback(ConnectCallback cb) { connect_cb_ = std::move(cb); }

  /// @brief Subscribe to a channel.
  bool Subscribe(const std::string& channel);

  /// @brief Unsubscribe from a channel.
  bool Unsubscribe(const std::string& channel);

  /// @brief Start the subscriber.
  void Start();

  /// @brief Stop the subscriber.
  void Stop();

  /// @brief Check whether the subscriber is connected.
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
