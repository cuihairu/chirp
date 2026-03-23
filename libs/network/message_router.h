#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "redis_client.h"

namespace chirp::network {

/// @brief Redis Pub/Sub message router.
/// Routes messages between distributed service instances.
class MessageRouter {
public:
  /// @brief Message callback type.
  using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;

  /// @brief Subscription callback type.
  using SubscribeCallback = std::function<void(const std::string& message)>;

  MessageRouter(asio::io_context& io,
                std::string redis_host,
                uint16_t redis_port);
  ~MessageRouter();

  /// @brief Start the router.
  bool Start();

  /// @brief Stop the router.
  void Stop();

  /// @brief Publish a message to a channel.
  bool Publish(const std::string& channel, const std::string& message);

  /// @brief Subscribe to a user's chat channel.
  bool SubscribeUserChat(const std::string& user_id, SubscribeCallback cb);

  /// @brief Subscribe to a group chat channel.
  bool SubscribeGroupChat(const std::string& group_id, SubscribeCallback cb);

  /// @brief Subscribe to a user's social channel.
  bool SubscribeUserSocial(const std::string& user_id, SubscribeCallback cb);

  /// @brief Subscribe to a kick-notification channel.
  bool SubscribeKickNotification(const std::string& instance_id, SubscribeCallback cb);

  /// @brief Unsubscribe from a channel.
  void Unsubscribe(const std::string& channel);

  /// @brief Send a chat message with local-first routing.
  bool SendChatMessage(const std::string& user_id, const std::string& message, std::function<bool(const std::string&)> local_send);

  /// @brief Broadcast a message to a group.
  bool BroadcastToGroup(const std::string& group_id, const std::string& message);

  const std::string& RedisHost() const { return redis_host_; }
  uint16_t RedisPort() const { return redis_port_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  asio::io_context& io_;
  std::string redis_host_;
  uint16_t redis_port_;
};

/// @brief Channel naming helpers.
struct RouterChannels {
  static std::string UserChat(const std::string& user_id) {
    return "chirp:chat:user:" + user_id;
  }

  static std::string GroupChat(const std::string& group_id) {
    return "chirp:chat:group:" + group_id;
  }

  static std::string UserSocial(const std::string& user_id) {
    return "chirp:social:user:" + user_id;
  }

  static std::string UserPresence(const std::string& user_id) {
    return "chirp:presence:user:" + user_id;
  }

  static std::string KickNotification(const std::string& instance_id) {
    return "chirp:kick:instance:" + instance_id;
  }

  static std::string ServiceRegister(const std::string& service, const std::string& instance_id) {
    return "chirp:service:" + service + ":" + instance_id;
  }
};

} // namespace chirp::network
