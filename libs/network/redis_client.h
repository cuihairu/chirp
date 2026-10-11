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

#include "network/redis_protocol.h"

namespace chirp::network {

/// @brief Synchronous Redis client for command operations.
/// Methods are virtual so tests can substitute throwing/misbehaving doubles.
///
/// Sockets are pooled and reused: up to [kDefaultMaxIdle] recently used
/// connections stay open, so a command round-trip no longer pays the
/// connect+close handshake every time (capacity_benchmark.md: the per-command
/// short connection is a primary single-thread CPU sink on the message path).
/// Checkout probes a pooled socket first — a dead peer (EOF / RST) or
/// unexpected bytes are discarded, a fresh connect is opened otherwise — so a
/// recycled socket is never handed out half-closed. A failed command discards
/// its socket rather than retrying, keeping today's at-most-once semantics for
/// writes such as RPUSH (a blind retry could apply a write twice).
///
/// Each connection owns its own io_context: every operation is synchronous
/// on the calling thread and no io loop is ever run. Asynchronous/coroutine
/// transports are deliberately out of scope here (see
/// docs/design-notes/asio_coroutine_adoption.md).
///
/// Not copyable/movable: the idle sockets are owned, exclusively used
/// connections guarded by one mutex.
class RedisClient {
public:
  static constexpr size_t kDefaultMaxIdle = 8;

  RedisClient(std::string host, uint16_t port, size_t max_idle = kDefaultMaxIdle);
  virtual ~RedisClient() = default;

  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;
  RedisClient(RedisClient&&) = delete;
  RedisClient& operator=(RedisClient&&) = delete;

  // Generic escape hatch for commands without a dedicated wrapper (e.g.
  // streams). Returns std::nullopt on connection or transport failure.
  virtual std::optional<RedisResp> Command(const std::vector<std::string>& args);

  // Basic commands
  virtual std::optional<std::string> Get(const std::string& key);
  virtual bool SetEx(const std::string& key, const std::string& value, int ttl_seconds);
  // SET without a TTL: durable data (e.g. relationship snapshots) must never
  // silently expire, which any TTL-based variant would eventually do.
  virtual bool Set(const std::string& key, const std::string& value);
  virtual bool Del(const std::string& key);

  // Pub/Sub commands
  virtual bool Publish(const std::string& channel, const std::string& message);

  // PUBLISH with the receiver count Redis reports: how many subscribers got
  // the message; -1 when the command failed (no connection / bad reply).
  virtual int64_t PublishCount(const std::string& channel, const std::string& message);

  // List commands
  bool RPush(const std::string& key, const std::string& value);
  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop);

  // Removes count occurrences of value from the list stored at key (count=1
  // scans head to tail). Returns how many elements were removed, or -1 when
  // the command failed (no connection / bad reply).
  virtual int64_t LRem(const std::string& key, int64_t count, const std::string& value);

  // Sets the list element at index to value. False when the command failed
  // (no connection, index out of range, or any reply other than +OK).
  virtual bool LSet(const std::string& key, int64_t index, const std::string& value);

  // Expiration commands
  bool Expire(const std::string& key, int ttl_seconds);

  // Keys command
  virtual std::vector<std::string> Keys(const std::string& pattern);

private:
  struct Connection {
    asio::io_context io;
    asio::ip::tcp::socket socket{io};
  };

  // Opens a new socket (resolver + connect). nullptr on any failure.
  std::unique_ptr<Connection> Connect();
  // Non-blocking liveness probe: would_block = open and idle, EOF/RST or
  // already-buffered bytes (protocol desync) = do not reuse.
  bool IsAlive(Connection& conn);
  // Pops a live pooled socket, or opens a new one.
  std::unique_ptr<Connection> Checkout();
  void Retire(std::unique_ptr<Connection> conn);
  void CloseConnection(std::unique_ptr<Connection> conn);

  std::string host_;
  uint16_t port_;
  size_t max_idle_;

  std::mutex pool_mu_;
  std::vector<std::unique_ptr<Connection>> idle_;
};

/// @brief Redis subscriber with multi-channel support.
class RedisSubscriber {
public:
  using MessageCallback = std::function<void(const std::string& channel, const std::string& payload)>;
  using ErrorCallback = std::function<void(const std::string& error)>;
  using ConnectCallback = std::function<void()>;

  RedisSubscriber(std::string host, uint16_t port);
  virtual ~RedisSubscriber();

  RedisSubscriber(const RedisSubscriber&) = delete;
  RedisSubscriber& operator=(const RedisSubscriber&) = delete;

  /// @brief Set the message callback.
  void SetMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

  /// @brief Set the error callback.
  void SetErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }

  /// @brief Set the connect callback.
  void SetConnectCallback(ConnectCallback cb) { connect_cb_ = std::move(cb); }

  /// @brief Subscribe to a channel.
  virtual bool Subscribe(const std::string& channel);

  /// @brief Unsubscribe from a channel.
  virtual bool Unsubscribe(const std::string& channel);

  /// @brief Start the subscriber.
  virtual void Start();

  /// @brief Stop the subscriber.
  virtual void Stop();

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
