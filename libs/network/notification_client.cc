#include "network/notification_client.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <asio.hpp>

#include "common/logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/common.pb.h"

namespace chirp {
namespace app_notification {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

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

}  // namespace

struct NotificationClient::Impl {
  struct Job {
    chirp::gateway::MsgID req_id{chirp::gateway::PUSH_NOTIFICATION_REQ};
    chirp::gateway::MsgID resp_id{chirp::gateway::PUSH_NOTIFICATION_RESP};
    std::string body;
    int64_t seq{0};
    DeliverFn deliver;
  };

  asio::io_context& main_io;
  std::string host;
  uint16_t port{0};

  std::mutex mu;
  std::condition_variable cv;
  std::deque<Job> q;
  bool stop{false};
  std::thread worker;

  Impl(asio::io_context& io, std::string h, uint16_t p)
      : main_io(io), host(std::move(h)), port(p) {}

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

      try {
        asio::io_context io;
        asio::ip::tcp::resolver resolver(io);
        asio::ip::tcp::socket sock(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(sock, endpoints);

        chirp::gateway::Packet pkt;
        pkt.set_msg_id(job.req_id);
        pkt.set_sequence(job.seq);
        pkt.set_body(job.body);

        auto out = chirp::network::ProtobufFraming::Encode(pkt);
        asio::write(sock, asio::buffer(out));

        std::string payload;
        if (!ReadFrame(sock, &payload)) {
          throw std::runtime_error("failed to read notification frame");
        }
        chirp::gateway::Packet resp_pkt;
        if (!resp_pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size())) ||
            resp_pkt.msg_id() != job.resp_id) {
          throw std::runtime_error("unexpected notification response");
        }

        asio::post(main_io,
                   [deliver = std::move(job.deliver),
                    body = resp_pkt.body()]() mutable { deliver(body, true); });
      } catch (const std::exception& e) {
        chirp::common::Logger::Instance().Warn(std::string("notification rpc failed: ") +
                                               e.what());
        asio::post(main_io,
                   [deliver = std::move(job.deliver)]() mutable { deliver("", false); });
      }
    }
  }
};

NotificationClient::NotificationClient(asio::io_context& main_io, std::string host, uint16_t port)
    : impl_(std::make_unique<Impl>(main_io, std::move(host), port)) {
  impl_->Start();
}

void DrainAndDropNotificationClientForTest(NotificationClient& client) {
  if (!client.impl_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(client.impl_->mu);
    client.impl_->stop = true;
  }
  client.impl_->cv.notify_all();
  if (client.impl_->worker.joinable()) {
    client.impl_->worker.join();
  }
  client.impl_.reset();
}

NotificationClient::~NotificationClient() {
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

void NotificationClient::Enqueue(chirp::gateway::MsgID req_id, chirp::gateway::MsgID resp_id,
                                 std::string body, int64_t seq, DeliverFn deliver) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->q.push_back({req_id, resp_id, std::move(body), seq, std::move(deliver)});
  }
  impl_->cv.notify_one();
}

void NotificationClient::AsyncPush(const PushNotificationRequest& req, int64_t seq,
                                   PushCallback cb) {
  Enqueue(chirp::gateway::PUSH_NOTIFICATION_REQ, chirp::gateway::PUSH_NOTIFICATION_RESP,
          req.SerializeAsString(), seq,
          [cb = std::move(cb)](const std::string& body, bool ok) mutable {
            PushNotificationResponse resp;
            if (!ok || !resp.ParseFromString(body)) {
              resp.set_code(chirp::common::INTERNAL_ERROR);
              resp.set_server_time(NowMs());
            }
            if (cb) {
              cb(resp);
            }
          });
}

void NotificationClient::AsyncRegisterDevice(const RegisterDeviceRequest& req, int64_t seq,
                                             RegisterCallback cb) {
  Enqueue(chirp::gateway::REGISTER_DEVICE_REQ, chirp::gateway::REGISTER_DEVICE_RESP,
          req.SerializeAsString(), seq,
          [cb = std::move(cb)](const std::string& body, bool ok) mutable {
            RegisterDeviceResponse resp;
            if (!ok || !resp.ParseFromString(body)) {
              resp.set_code(chirp::common::INTERNAL_ERROR);
              resp.set_server_time(NowMs());
            }
            if (cb) {
              cb(resp);
            }
          });
}

void NotificationClient::AsyncUnregisterDevice(const UnregisterDeviceRequest& req, int64_t seq,
                                               UnregisterCallback cb) {
  Enqueue(chirp::gateway::UNREGISTER_DEVICE_REQ, chirp::gateway::UNREGISTER_DEVICE_RESP,
          req.SerializeAsString(), seq,
          [cb = std::move(cb)](const std::string& body, bool ok) mutable {
            UnregisterDeviceResponse resp;
            if (!ok || !resp.ParseFromString(body)) {
              resp.set_code(chirp::common::INTERNAL_ERROR);
              resp.set_server_time(NowMs());
            }
            if (cb) {
              cb(resp);
            }
          });
}

void NotificationClient::AsyncUpdateDeviceToken(const UpdateDeviceTokenRequest& req, int64_t seq,
                                                UpdateTokenCallback cb) {
  Enqueue(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, chirp::gateway::UPDATE_DEVICE_TOKEN_RESP,
          req.SerializeAsString(), seq,
          [cb = std::move(cb)](const std::string& body, bool ok) mutable {
            UpdateDeviceTokenResponse resp;
            if (!ok || !resp.ParseFromString(body)) {
              resp.set_code(chirp::common::INTERNAL_ERROR);
              resp.set_server_time(NowMs());
            }
            if (cb) {
              cb(resp);
            }
          });
}

void NotificationClient::AsyncGetUserDevices(const GetUserDevicesRequest& req, int64_t seq,
                                             DevicesCallback cb) {
  Enqueue(chirp::gateway::GET_USER_DEVICES_REQ, chirp::gateway::GET_USER_DEVICES_RESP,
          req.SerializeAsString(), seq,
          [cb = std::move(cb)](const std::string& body, bool ok) mutable {
            GetUserDevicesResponse resp;
            if (!ok || !resp.ParseFromString(body)) {
              resp.set_code(chirp::common::INTERNAL_ERROR);
            }
            if (cb) {
              cb(resp);
            }
          });
}

}  // namespace app_notification
}  // namespace chirp
