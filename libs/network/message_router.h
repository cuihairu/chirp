#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "redis_client.h"

namespace chirp::network {

/// @brief Redis Pub/Sub 消息路由器
/// 用于分布式服务实例之间的消息转发
class MessageRouter {
public:
  /// @brief 消息回调函数类型
  using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;

  /// @brief 订阅回调函数类型
  using SubscribeCallback = std::function<void(const std::string& message)>;

  MessageRouter(asio::io_context& io,
                std::string redis_host,
                uint16_t redis_port);
  ~MessageRouter();

  /// @brief 启动路由器
  bool Start();

  /// @brief 停止路由器
  void Stop();

  /// @brief 发布消息到指定频道
  /// @param channel 频道名称
  /// @param message 消息内容
  /// @return 是否发布成功
  bool Publish(const std::string& channel, const std::string& message);

  /// @brief 订阅用户聊天消息频道
  /// 频道格式: chirp:chat:user:{user_id}
  bool SubscribeUserChat(const std::string& user_id, SubscribeCallback cb);

  /// @brief 订阅群组聊天消息频道
  /// 频道格式: chirp:chat:group:{group_id}
  bool SubscribeGroupChat(const std::string& group_id, SubscribeCallback cb);

  /// @brief 订阅用户社交消息频道
  /// 频道格式: chirp:social:user:{user_id}
  bool SubscribeUserSocial(const std::string& user_id, SubscribeCallback cb);

  /// @brief 订阅踢人通知频道
  /// 频道格式: chirp:kick:instance:{instance_id}
  bool SubscribeKickNotification(const std::string& instance_id, SubscribeCallback cb);

  /// @brief 取消订阅
  void Unsubscribe(const std::string& channel);

  /// @brief 发送聊天消息给用户（智能路由）
  /// 优先本地投递，失败则通过 Redis 转发
  bool SendChatMessage(const std::string& user_id, const std::string& message, std::function<bool(const std::string&)> local_send);

  /// @brief 广播消息到群组
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

/// @brief 频道命名工具
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
