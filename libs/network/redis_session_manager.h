#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <asio.hpp>

#include "network/redis_client.h"

namespace chirp::gateway {

class RedisSessionManager {
public:
  using KickCallback = std::function<void(const std::string& user_id)>;
  using ClaimCallback = std::function<void(std::optional<std::string> previous_owner)>;
  /// @brief Optional factory used by tests to substitute the Redis client
  /// (e.g. a double whose methods throw).
  using ClientFactory = std::function<std::unique_ptr<chirp::network::RedisClient>()>;

  RedisSessionManager(asio::io_context& main_io,
                      std::string redis_host,
                      uint16_t redis_port,
                      std::string instance_id,
                      int session_ttl_seconds,
                      KickCallback on_kick,
                      ClientFactory client_factory = nullptr);
  ~RedisSessionManager();

  void AsyncClaim(const std::string& user_id, ClaimCallback cb);
  void AsyncRelease(const std::string& user_id);

  const std::string& InstanceId() const { return instance_id_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::string instance_id_;
};

} // namespace chirp::gateway

