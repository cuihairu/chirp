#include "message_router.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <set>
#include <asio.hpp>

#include "common/logger.h"

namespace chirp::network {

namespace {

constexpr int kConnectRetryIntervalMs = 5000;
constexpr int kSubscribeTimeoutMs = 5000;

} // namespace

struct MessageRouter::Impl {
  asio::io_context& io;
  std::string host;
  uint16_t port;

  // Redis 客户端（用于发布）
  std::unique_ptr<RedisClient> publisher;

  // Redis 订阅者
  std::unique_ptr<RedisSubscriber> subscriber;

  // 订阅的频道集合
  std::set<std::string> subscribed_channels;

  // 订阅回调映射
  std::unordered_map<std::string, SubscribeCallback> subscriptions;

  // 运行状态
  std::atomic<bool> running{false};
  std::atomic<bool> connected{false};

  Impl(asio::io_context& io, std::string redis_host, uint16_t redis_port)
      : io(io), host(std::move(redis_host)), port(redis_port) {
    publisher = std::make_unique<RedisClient>(host, port);
    subscriber = std::make_unique<RedisSubscriber>(host, port);

    // 设置订阅者回调
    subscriber->SetMessageCallback([this, &io](const std::string& channel, const std::string& message) {
      auto it = subscriptions.find(channel);
      if (it != subscriptions.end() && it->second) {
        // 将回调投递到主 io_context
        asio::post(io, [cb = it->second, msg = message]() {
          cb(msg);
        });
      }
    });

    subscriber->SetErrorCallback([this](const std::string& error) {
      chirp::common::Logger::Instance().Warn("MessageRouter Redis error: " + error);
      connected = false;
    });

    subscriber->SetConnectCallback([this]() {
      chirp::common::Logger::Instance().Info("MessageRouter Redis connected");
      connected = true;

      // 重新订阅之前的频道
      for (const auto& channel : subscribed_channels) {
        subscriber->Subscribe(channel);
      }
    });
  }

  bool Start() {
    try {
      subscriber->Start();
      running = true;
      return true;
    } catch (const std::exception& e) {
      chirp::common::Logger::Instance().Error("Failed to start MessageRouter: " + std::string(e.what()));
      return false;
    }
  }

  void Stop() {
    running = false;
    if (subscriber) {
      subscriber->Stop();
    }
    subscribed_channels.clear();
    subscriptions.clear();
  }
};

MessageRouter::MessageRouter(asio::io_context& io,
                             std::string redis_host,
                             uint16_t redis_port)
    : io_(io), redis_host_(std::move(redis_host)), redis_port_(redis_port) {
  impl_ = std::make_unique<Impl>(io_, redis_host_, redis_port_);
}

MessageRouter::~MessageRouter() {
  Stop();
}

bool MessageRouter::Start() {
  return impl_->Start();
}

void MessageRouter::Stop() {
  impl_->Stop();
}

bool MessageRouter::Publish(const std::string& channel, const std::string& message) {
  if (!impl_->publisher) {
    return false;
  }

  try {
    return impl_->publisher->Publish(channel, message);
  } catch (const std::exception& e) {
    chirp::common::Logger::Instance().Error("MessageRouter::Publish failed: " + std::string(e.what()));
    return false;
  }
}

bool MessageRouter::SubscribeUserChat(const std::string& user_id, SubscribeCallback cb) {
  std::string channel = RouterChannels::UserChat(user_id);
  impl_->subscriptions[channel] = std::move(cb);
  impl_->subscribed_channels.insert(channel);

  if (impl_->subscriber) {
    return impl_->subscriber->Subscribe(channel);
  }
  return true;
}

bool MessageRouter::SubscribeGroupChat(const std::string& group_id, SubscribeCallback cb) {
  std::string channel = RouterChannels::GroupChat(group_id);
  impl_->subscriptions[channel] = std::move(cb);
  impl_->subscribed_channels.insert(channel);

  if (impl_->subscriber) {
    return impl_->subscriber->Subscribe(channel);
  }
  return true;
}

bool MessageRouter::SubscribeUserSocial(const std::string& user_id, SubscribeCallback cb) {
  std::string channel = RouterChannels::UserSocial(user_id);
  impl_->subscriptions[channel] = std::move(cb);
  impl_->subscribed_channels.insert(channel);

  if (impl_->subscriber) {
    return impl_->subscriber->Subscribe(channel);
  }
  return true;
}

bool MessageRouter::SubscribeKickNotification(const std::string& instance_id, SubscribeCallback cb) {
  std::string channel = RouterChannels::KickNotification(instance_id);
  impl_->subscriptions[channel] = std::move(cb);
  impl_->subscribed_channels.insert(channel);

  if (impl_->subscriber) {
    return impl_->subscriber->Subscribe(channel);
  }
  return true;
}

void MessageRouter::Unsubscribe(const std::string& channel) {
  impl_->subscriptions.erase(channel);
  impl_->subscribed_channels.erase(channel);

  if (impl_->subscriber) {
    impl_->subscriber->Unsubscribe(channel);
  }
}

bool MessageRouter::SendChatMessage(const std::string& user_id,
                                    const std::string& message,
                                    std::function<bool(const std::string&)> local_send) {
  // 1. 尝试本地投递
  if (local_send && local_send(user_id)) {
    return true;
  }

  // 2. 本地投递失败，通过 Redis Pub/Sub 转发
  std::string channel = RouterChannels::UserChat(user_id);
  return Publish(channel, message);
}

bool MessageRouter::BroadcastToGroup(const std::string& group_id, const std::string& message) {
  std::string channel = RouterChannels::GroupChat(group_id);
  return Publish(channel, message);
}

} // namespace chirp::network
