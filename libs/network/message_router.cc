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

  // Guards the two containers above: mutated on the caller's thread
  // (Subscribe*/Unsubscribe/Stop), read on the subscriber thread (message
  // dispatch, reconnect re-subscribe). Never held across subscriber->()
  // calls: those take the subscriber's sock_mu_ and can re-enter callbacks
  // while holding it, which would invert the lock order and deadlock.
  mutable std::mutex state_mu;

  // 运行状态
  std::atomic<bool> running{false};
  std::atomic<bool> connected{false};

  Impl(asio::io_context& io, std::string redis_host, uint16_t redis_port,
       const MessageRouter::PublisherFactory& publisher_factory,
       const MessageRouter::SubscriberFactory& subscriber_factory)
      : io(io), host(std::move(redis_host)), port(redis_port) {
    if (publisher_factory) {
      publisher = publisher_factory();
    } else if (!host.empty()) {
      publisher = std::make_unique<RedisClient>(host, port);
    }
    if (subscriber_factory) {
      subscriber = subscriber_factory();
    } else if (!host.empty()) {
      subscriber = std::make_unique<RedisSubscriber>(host, port);
    }
    if (!subscriber) {
      // No backend: nothing to wire up.
      return;
    }

    // 设置订阅者回调
    subscriber->SetMessageCallback([this, &io](const std::string& channel, const std::string& message) {
      SubscribeCallback cb;
      {
        std::lock_guard<std::mutex> lock(state_mu);
        auto it = subscriptions.find(channel);
        if (it != subscriptions.end()) {
          cb = it->second;
        }
      }
      if (cb) {
        // 将回调投递到主 io_context
        asio::post(io, [cb = std::move(cb), msg = message]() {
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

      // 重新订阅之前的频道：先在锁内取快照，锁外再发 SUBSCRIBE，
      // 与调用方的订阅/退订互不持锁等待。
      std::vector<std::string> channels;
      {
        std::lock_guard<std::mutex> lock(state_mu);
        channels.assign(subscribed_channels.begin(), subscribed_channels.end());
      }
      for (const auto& channel : channels) {
        subscriber->Subscribe(channel);
      }
    });
  }

  // Shared tail of the four typed Subscribe* entry points: record the
  // callback and channel under the lock, then send SUBSCRIBE outside it.
  bool SubscribeChannel(const std::string& channel, SubscribeCallback cb) {
    {
      std::lock_guard<std::mutex> lock(state_mu);
      subscriptions[channel] = std::move(cb);
      subscribed_channels.insert(channel);
    }

    if (subscriber) {
      return subscriber->Subscribe(channel);
    }
    return true;
  }

  void UnsubscribeChannel(const std::string& channel) {
    {
      std::lock_guard<std::mutex> lock(state_mu);
      subscriptions.erase(channel);
      subscribed_channels.erase(channel);
    }

    if (subscriber) {
      subscriber->Unsubscribe(channel);
    }
  }

  bool Start() {
    if (!subscriber) {
      // Local-only mode (no Redis backend): nothing to start.
      running = true;
      return true;
    }
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
    // subscriber 线程已在 Stop() 内 join，此处已无并发访问；仍走锁保持纪律。
    std::lock_guard<std::mutex> lock(state_mu);
    subscribed_channels.clear();
    subscriptions.clear();
  }
};

MessageRouter::MessageRouter(asio::io_context& io,
                             std::string redis_host,
                             uint16_t redis_port,
                             PublisherFactory publisher_factory,
                             SubscriberFactory subscriber_factory)
    : io_(io), redis_host_(std::move(redis_host)), redis_port_(redis_port) {
  impl_ = std::make_unique<Impl>(io_, redis_host_, redis_port_,
                                 publisher_factory, subscriber_factory);
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
  return PublishCount(channel, message) >= 0;
}

int64_t MessageRouter::PublishCount(const std::string& channel, const std::string& message) {
  if (!impl_->publisher) {
    return -1;
  }

  try {
    return impl_->publisher->PublishCount(channel, message);
  } catch (const std::exception& e) {
    chirp::common::Logger::Instance().Error("MessageRouter::Publish failed: " + std::string(e.what()));
    return -1;
  }
}

bool MessageRouter::SubscribeUserChat(const std::string& user_id, SubscribeCallback cb) {
  return impl_->SubscribeChannel(RouterChannels::UserChat(user_id), std::move(cb));
}

bool MessageRouter::SubscribeGroupChat(const std::string& group_id, SubscribeCallback cb) {
  return impl_->SubscribeChannel(RouterChannels::GroupChat(group_id), std::move(cb));
}

bool MessageRouter::SubscribeUserSocial(const std::string& user_id, SubscribeCallback cb) {
  return impl_->SubscribeChannel(RouterChannels::UserSocial(user_id), std::move(cb));
}

bool MessageRouter::SubscribeKickNotification(const std::string& instance_id, SubscribeCallback cb) {
  return impl_->SubscribeChannel(RouterChannels::KickNotification(instance_id), std::move(cb));
}

void MessageRouter::Unsubscribe(const std::string& channel) {
  impl_->UnsubscribeChannel(channel);
}

bool MessageRouter::SendChatMessage(const std::string& user_id,
                                    const std::string& message,
                                    std::function<bool(const std::string&)> local_send) {
  return SendChatMessageCount(user_id, message, std::move(local_send)) >= 0;
}

int64_t MessageRouter::SendChatMessageCount(const std::string& user_id,
                                            const std::string& message,
                                            std::function<bool(const std::string&)> local_send) {
  // 1. 尝试本地投递
  if (local_send && local_send(user_id)) {
    return 1;
  }

  // 2. 本地投递失败，通过 Redis Pub/Sub 转发
  std::string channel = RouterChannels::UserChat(user_id);
  return PublishCount(channel, message);
}

bool MessageRouter::BroadcastToGroup(const std::string& group_id, const std::string& message) {
  std::string channel = RouterChannels::GroupChat(group_id);
  return Publish(channel, message);
}

} // namespace chirp::network
