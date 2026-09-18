#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <asio.hpp>

#include "network/redis_client.h"

namespace chirp::gateway {

/// @brief Cross-instance session claims: which edge instance owns which
/// (user, device) pair. The claim key is "chirp:sess:<user_id>\x1F<device_id>"
/// (device id normalized) and the value is the owning instance id; claiming a
/// key held by another instance publishes "user\x1Fdevice" on that instance's
/// private kick channel ("chirp:kick:<instance_id>") so it can drop exactly
/// the session logged in from the same device. User ids containing \x1F are
/// not supported (they would make the key/payload ambiguous). Known
/// limitation: claim is three independent commands (GET/PUBLISH/SETEX), not
/// a transaction - concurrent claims of the same pair may kick twice.
class RedisSessionManager {
public:
  /// @brief Invoked on the main io_context when another instance claimed a
  /// (user, device) pair previously held by this one. The device id arrives
  /// normalized; an empty device means the payload could not be resolved to
  /// one and the callback should treat the lookup as a no-op.
  using KickCallback = std::function<void(const std::string& user_id, const std::string& device_id)>;
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

  /// @brief Claims (user_id, device_id) for this instance. The device id is
  /// normalized here (NormalizeDeviceId), so callers may pass the raw login
  /// field. previous_owner carries the instance that held the key before
  /// this claim, or nullopt when Redis is unreachable.
  void AsyncClaim(const std::string& user_id, const std::string& device_id, ClaimCallback cb);
  /// @brief Releases the (user_id, device_id) claim, but only if this
  /// instance still owns it. Device id normalized as in AsyncClaim.
  void AsyncRelease(const std::string& user_id, const std::string& device_id);

  const std::string& InstanceId() const { return instance_id_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  std::string instance_id_;
};

} // namespace chirp::gateway
