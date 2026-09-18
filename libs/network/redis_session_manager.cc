#include "network/redis_session_manager.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

#include <asio.hpp>

#include "common/logger.h"
#include "network/session_registry.h"

namespace chirp::gateway {
namespace {

// Same separator as the Redis claim keys' only structured payload: user and
// device ids are free-form strings, so they are joined with \x1F (which
// neither field may contain - see the class comment in the header).
constexpr char kIdSep = '\x1F';

std::string SessionKey(const std::string& user_id, const std::string& device_id) {
  return "chirp:sess:" + user_id + kIdSep + device_id;
}
std::string KickChannel(const std::string& instance_id) { return "chirp:kick:" + instance_id; }
std::string KickPayload(const std::string& user_id, const std::string& device_id) {
  return user_id + kIdSep + device_id;
}
// Splits "user<sep>device" on the first separator. Returns false for
// payloads without one (e.g. user-level kicks from a pre-device instance,
// which are dropped instead of kicking an arbitrary local session).
bool ParseKickPayload(const std::string& payload, std::string* user_id, std::string* device_id) {
  const auto sep = payload.find(kIdSep);
  if (sep == std::string::npos) {
    return false;
  }
  *user_id = payload.substr(0, sep);
  *device_id = payload.substr(sep + 1);
  return true;
}

} // namespace

struct RedisSessionManager::Impl {
  struct Job {
    enum class Type { kClaim, kRelease };
    Type type{Type::kClaim};
    std::string user_id;
    std::string device_id;
    ClaimCallback cb;
  };

  asio::io_context& main_io;
  std::unique_ptr<chirp::network::RedisClient> client;
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
       KickCallback kick_cb,
       const ClientFactory& client_factory)
      : main_io(io),
        sub(host, port),
        instance_id(std::move(inst)),
        ttl(ttl_seconds),
        on_kick(std::move(kick_cb)) {
    if (client_factory) {
      client = client_factory();
    } else {
      client = std::make_unique<chirp::network::RedisClient>(std::move(host), port);
    }
  }

  void Start() {
    // Set message callback before starting
    sub.SetMessageCallback([this](const std::string& /*ch*/, const std::string& payload) {
      std::string user_id;
      std::string device_id;
      // Parsed on the subscriber thread: only local copies are touched, and
      // malformed payloads (pre-device writers) are dropped with a warning.
      if (!ParseKickPayload(payload, &user_id, &device_id)) {
        chirp::common::Logger::Instance().Warn("dropping malformed kick payload on " +
                                               KickChannel(instance_id));
        return;
      }
      asio::post(main_io, [cb = on_kick, user_id, device_id] {
        if (cb) {
          cb(user_id, device_id);
        }
      });
    });
    // Subscribe once the connection is up: calling Subscribe() before
    // Start() is a silent no-op (the socket is not open yet), and without
    // re-subscribing here the manager would never receive kick messages.
    sub.SetConnectCallback([this] {
      sub.Subscribe(KickChannel(instance_id));
    });
    // Start the subscriber
    sub.Start();
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
          std::optional<std::string> prev = client->Get(SessionKey(job.user_id, job.device_id));
          if (prev && *prev != instance_id) {
            client->Publish(KickChannel(*prev), KickPayload(job.user_id, job.device_id));
          }
          client->SetEx(SessionKey(job.user_id, job.device_id), instance_id, ttl);

          asio::post(main_io, [cb = std::move(job.cb), prev]() mutable {
            if (cb) {
              cb(prev);
            }
          });
        } else {
          auto cur = client->Get(SessionKey(job.user_id, job.device_id));
          if (cur && *cur == instance_id) {
            client->Del(SessionKey(job.user_id, job.device_id));
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
                                         KickCallback on_kick,
                                         ClientFactory client_factory)
    : instance_id_(std::move(instance_id)) {
  impl_ = std::make_unique<Impl>(
      main_io, std::move(redis_host), redis_port, instance_id_, session_ttl_seconds,
      std::move(on_kick), std::move(client_factory));
  impl_->Start();
}

RedisSessionManager::~RedisSessionManager() {
  if (impl_) {
    impl_->Stop();
  }
}

void RedisSessionManager::AsyncClaim(const std::string& user_id,
                                     const std::string& device_id,
                                     ClaimCallback cb) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->q.push_back(Impl::Job{Impl::Job::Type::kClaim, user_id,
                                 chirp::network::NormalizeDeviceId(device_id), std::move(cb)});
  }
  impl_->cv.notify_one();
}

void RedisSessionManager::AsyncRelease(const std::string& user_id, const std::string& device_id) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->q.push_back(Impl::Job{Impl::Job::Type::kRelease, user_id,
                                 chirp::network::NormalizeDeviceId(device_id), {}});
  }
  impl_->cv.notify_one();
}

} // namespace chirp::gateway
