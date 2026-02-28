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

namespace chirp::network {

class RedisClient {
public:
  RedisClient(std::string host, uint16_t port);

  std::optional<std::string> Get(const std::string& key);
  bool SetEx(const std::string& key, const std::string& value, int ttl_seconds);
  bool Del(const std::string& key);
  bool Publish(const std::string& channel, const std::string& message);
  bool RPush(const std::string& key, const std::string& value);
  bool Expire(const std::string& key, int ttl_seconds);
  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop);

private:
  std::string host_;
  uint16_t port_;
};

class RedisSubscriber {
public:
  using MessageCallback = std::function<void(const std::string& channel, const std::string& payload)>;

  RedisSubscriber(std::string host, uint16_t port);
  ~RedisSubscriber();

  RedisSubscriber(const RedisSubscriber&) = delete;
  RedisSubscriber& operator=(const RedisSubscriber&) = delete;

  void Start(const std::string& channel, MessageCallback cb);
  void Stop();

private:
  void Run();

  std::string host_;
  uint16_t port_;
  std::string channel_;
  MessageCallback cb_;

  std::atomic<bool> stop_{false};
  std::mutex sock_mu_;
  std::shared_ptr<void> sock_holder_;
  std::thread th_;
};

} // namespace chirp::network
