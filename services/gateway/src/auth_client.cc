#include "auth_client.h"

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#include <asio.hpp>

#include "common/logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::gateway {
namespace {

bool ReadFrame(asio::ip::tcp::socket& sock, std::string* payload) {
  uint8_t len_be[4];
  asio::error_code ec;
  asio::read(sock, asio::buffer(len_be, 4), ec);
  if (ec) {
    return false;
  }
  const uint32_t len = chirp::network::ReadU32BE(len_be);
  payload->resize(len);
  asio::read(sock, asio::buffer(payload->data(), payload->size()), ec);
  return !ec;
}

} // namespace

struct AuthClient::Impl {
  struct Job {
    enum class Type { kLogin, kLogout };
    Type type{Type::kLogin};
    chirp::auth::LoginRequest login_req;
    chirp::auth::LogoutRequest logout_req;
    int64_t seq{0};
    LoginCallback cb;
    LogoutCallback logout_cb;
  };

  asio::io_context& main_io;
  std::string host;
  uint16_t port{0};

  std::mutex mu;
  std::condition_variable cv;
  std::deque<Job> q;
  bool stop{false};
  std::thread worker;

  Impl(asio::io_context& io, std::string h, uint16_t p) : main_io(io), host(std::move(h)), port(p) {}

  void Start() { worker = std::thread([this] { Run(); }); }

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

      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

      try {
        asio::io_context io;
        asio::ip::tcp::resolver resolver(io);
        asio::ip::tcp::socket sock(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(sock, endpoints);

        chirp::gateway::Packet pkt;
        pkt.set_msg_id(job.type == Job::Type::kLogin ? chirp::gateway::LOGIN_REQ : chirp::gateway::LOGOUT_REQ);
        pkt.set_sequence(job.seq);
        pkt.set_body(job.type == Job::Type::kLogin ? job.login_req.SerializeAsString()
                                                   : job.logout_req.SerializeAsString());

        auto out = chirp::network::ProtobufFraming::Encode(pkt);
        asio::write(sock, asio::buffer(out));

        std::string payload;
        if (!ReadFrame(sock, &payload)) {
          throw std::runtime_error("failed to read auth frame");
        }

        chirp::gateway::Packet resp_pkt;
        if (!resp_pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          throw std::runtime_error("failed to parse auth response packet");
        }

        if (job.type == Job::Type::kLogin) {
          chirp::auth::LoginResponse resp;
          resp.set_server_time(now_ms);
          if (resp_pkt.msg_id() != chirp::gateway::LOGIN_RESP ||
              !resp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()))) {
            resp.set_code(chirp::common::INTERNAL_ERROR);
          }

          asio::post(main_io, [cb = std::move(job.cb), resp = std::move(resp)]() mutable {
            if (cb) {
              cb(resp);
            }
          });
        } else {
          chirp::auth::LogoutResponse resp;
          resp.set_server_time(now_ms);
          if (resp_pkt.msg_id() != chirp::gateway::LOGOUT_RESP ||
              !resp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()))) {
            resp.set_code(chirp::common::INTERNAL_ERROR);
          }

          asio::post(main_io, [cb = std::move(job.logout_cb), resp = std::move(resp)]() mutable {
            if (cb) {
              cb(resp);
            }
          });
        }
      } catch (const std::exception& e) {
        chirp::common::Logger::Instance().Warn(std::string("auth rpc failed: ") + e.what());
        if (job.type == Job::Type::kLogin) {
          chirp::auth::LoginResponse resp;
          resp.set_server_time(now_ms);
          resp.set_code(chirp::common::INTERNAL_ERROR);
          asio::post(main_io, [cb = std::move(job.cb), resp = std::move(resp)]() mutable {
            if (cb) {
              cb(resp);
            }
          });
        } else {
          chirp::auth::LogoutResponse resp;
          resp.set_server_time(now_ms);
          resp.set_code(chirp::common::INTERNAL_ERROR);
          asio::post(main_io, [cb = std::move(job.logout_cb), resp = std::move(resp)]() mutable {
            if (cb) {
              cb(resp);
            }
          });
        }
      }
    }
  }
};

AuthClient::AuthClient(asio::io_context& main_io, std::string host, uint16_t port)
    : impl_(std::make_unique<Impl>(main_io, std::move(host), port)) {
  impl_->Start();
}

AuthClient::~AuthClient() {
  if (!impl_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->stop = true;
  }
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
}

void AuthClient::AsyncLogin(const chirp::auth::LoginRequest& req, int64_t seq, LoginCallback cb) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Impl::Job job;
    job.type = Impl::Job::Type::kLogin;
    job.login_req = req;
    job.seq = seq;
    job.cb = std::move(cb);
    impl_->q.push_back(std::move(job));
  }
  impl_->cv.notify_one();
}

void AuthClient::AsyncLogout(const chirp::auth::LogoutRequest& req, int64_t seq, LogoutCallback cb) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    Impl::Job job;
    job.type = Impl::Job::Type::kLogout;
    job.logout_req = req;
    job.seq = seq;
    job.logout_cb = std::move(cb);
    impl_->q.push_back(std::move(job));
  }
  impl_->cv.notify_one();
}

} // namespace chirp::gateway
