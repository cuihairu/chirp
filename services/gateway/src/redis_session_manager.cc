#include "redis_session_manager.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

#include <asio.hpp>

#include "common/logger.h"

namespace chirp::gateway {
namespace {

std::string SessionKey(const std::string& user_id) { return "chirp:sess:" + user_id; }
std::string KickChannel(const std::string& instance_id) { return "chirp:kick:" + instance_id; }

} // namespace

struct RedisSessionManager::Impl {
  struct Job {
    enum class Type { kClaim, kRelease };
    Type type{Type::kClaim};
    std::string user_id;
    ClaimCallback cb;
  };

  asio::io_context& main_io;
  chirp::network::RedisClient client;
  chirp::network::RedisSubscriber sub;
  std::string instance_id;
  int ttl{3600};
  KickCallback on_kick;

  std::mutex mu;
  std::condition_variable cv;
  std::deque<Job> q;
  bool stop{false};
  std::thread worker;

  Impl(asio::io_context& io,
       std::string host,
       uint16_t port,
       std::string inst,
       int ttl_seconds,
       KickCallback kick_cb)
      : main_io(io),
        client(host, port),
        sub(std::move(host), port),
        instance_id(std::move(inst)),
        ttl(ttl_seconds),
        on_kick(std::move(kick_cb)) {}

  void Start() {
    sub.Start(KickChannel(instance_id), [this](const std::string& /*ch*/, const std::string& payload) {
      asio::post(main_io, [cb = on_kick, user_id = payload] {
        if (cb) {
          cb(user_id);
        }
      });
    });
    worker = std::thread([this] { Run(); });
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mu);
      stop = true;
    }
    cv.notify_all();
    sub.Stop();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void Run() {
    while (true) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return stop || !q.empty(); });
        if (stop && q.empty()) {
          return;
        }
        job = std::move(q.front());
        q.pop_front();
      }

      try {
        if (job.type == Job::Type::kClaim) {
          std::optional<std::string> prev = client.Get(SessionKey(job.user_id));
          if (prev && *prev != instance_id) {
            client.Publish(KickChannel(*prev), job.user_id);
          }
          client.SetEx(SessionKey(job.user_id), instance_id, ttl);

          asio::post(main_io, [cb = std::move(job.cb), prev]() mutable {
            if (cb) {
              cb(prev);
            }
          });
        } else {
          auto cur = client.Get(SessionKey(job.user_id));
          if (cur && *cur == instance_id) {
            client.Del(SessionKey(job.user_id));
          }
        }
      } catch (const std::exception& e) {
        chirp::common::Logger::Instance().Warn(std::string("redis session job failed: ") + e.what());
        if (job.type == Job::Type::kClaim) {
          asio::post(main_io, [cb = std::move(job.cb)]() mutable {
            if (cb) {
              cb(std::nullopt);
            }
          });
        }
      } catch (...) {
        chirp::common::Logger::Instance().Warn("redis session job failed: unknown error");
        if (job.type == Job::Type::kClaim) {
          asio::post(main_io, [cb = std::move(job.cb)]() mutable {
            if (cb) {
              cb(std::nullopt);
            }
          });
        }
      }
    }
  }
};

RedisSessionManager::RedisSessionManager(asio::io_context& main_io,
                                         std::string redis_host,
                                         uint16_t redis_port,
                                         std::string instance_id,
                                         int session_ttl_seconds,
                                         KickCallback on_kick)
    : instance_id_(std::move(instance_id)) {
  impl_ = std::make_unique<Impl>(
      main_io, std::move(redis_host), redis_port, instance_id_, session_ttl_seconds, std::move(on_kick));
  impl_->Start();
}

RedisSessionManager::~RedisSessionManager() {
  if (impl_) {
    impl_->Stop();
  }
}

void RedisSessionManager::AsyncClaim(const std::string& user_id, ClaimCallback cb) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->q.push_back(Impl::Job{Impl::Job::Type::kClaim, user_id, std::move(cb)});
  }
  impl_->cv.notify_one();
}

void RedisSessionManager::AsyncRelease(const std::string& user_id) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->q.push_back(Impl::Job{Impl::Job::Type::kRelease, user_id, {}});
  }
  impl_->cv.notify_one();
}

} // namespace chirp::gateway
