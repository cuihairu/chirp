// Loopback tests for the notification plane RPC client (mirrors
// gateway_extra_test.cc) plus failure injection for every defensive branch.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <thread>

#include <asio.hpp>

#include "network/protobuf_framing.h"
#include "notification_client.h"
#include "notification_handlers.h"
#include "notification_service.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

using chirp::notification::NotificationClient;

namespace {

// 127.0.0.1:1 - resolves locally, refuses instantly, safe for failure paths.
constexpr const char* kRefusedHost = "127.0.0.1";
constexpr uint16_t kRefusedPort = 1;

// Pumps the io_context while waiting for a future, so callbacks posted by
// background workers get executed on this thread.
template <typename Future>
bool SpinIoFor(asio::io_context& io, Future& future, int timeout_ms = 3000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    io.poll();
    io.restart();
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

class FakeNotificationServer {
 public:
  using Handler = std::function<chirp::gateway::Packet(const chirp::gateway::Packet&)>;

  explicit FakeNotificationServer(Handler handler)
      : handler_(std::move(handler)),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeNotificationServer() {
    asio::post(io_, [this] {
      asio::error_code ec;
      acceptor_.close(ec);
      if (socket_) {
        socket_->close(ec);
      }
    });
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint16_t port() const { return port_; }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      socket_ = sock;
      DoRead(sock);
      DoAccept();  // the client opens one connection per job
    });
  }

  void DoRead(std::shared_ptr<asio::ip::tcp::socket> sock) {
    sock->async_read_some(asio::buffer(buf_.data() + partial_, buf_.size() - partial_),
                          [this, sock](const std::error_code& ec, std::size_t n) {
                            if (ec) {
                              return;
                            }
                            partial_ += n;
                            Consume(sock);
                            if (sock->is_open()) {
                              DoRead(sock);
                            }
                          });
  }

  void Consume(const std::shared_ptr<asio::ip::tcp::socket>& sock) {
    while (partial_ >= 4) {
      const uint32_t len =
          (static_cast<uint32_t>(static_cast<uint8_t>(buf_[0])) << 24) |
          (static_cast<uint32_t>(static_cast<uint8_t>(buf_[1])) << 16) |
          (static_cast<uint32_t>(static_cast<uint8_t>(buf_[2])) << 8) |
          static_cast<uint32_t>(static_cast<uint8_t>(buf_[3]));
      if (partial_ < 4u + len) {
        break;
      }
      chirp::gateway::Packet pkt;
      if (pkt.ParseFromArray(buf_.data() + 4, static_cast<int>(len))) {
        chirp::gateway::Packet resp = handler_(pkt);
        auto framed = chirp::network::ProtobufFraming::Encode(resp);
        asio::error_code ec;
        asio::write(*sock, asio::buffer(framed), ec);
      }
      partial_ -= 4 + len;
      std::memmove(buf_.data(), buf_.data() + 4 + len, partial_);
    }
  }

  Handler handler_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::array<uint8_t, 8192> buf_{};
  size_t partial_ = 0;
  std::thread thread_;
};

// Accepts one connection and closes it right away.
class DroppingNotificationServer {
 public:
  DroppingNotificationServer()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }
  ~DroppingNotificationServer() {
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  uint16_t port() const { return port_; }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      asio::error_code ignore;
      sock->close(ignore);
    });
  }
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
};

// A service with one registered tokenless device (the stub transport makes
// its sends succeed) served by the real packet handlers.
struct Loopback {
  Loopback()
      : server([this](const chirp::gateway::Packet& pkt) {
          chirp::gateway::Packet resp;
          handlers.HandlePacket(pkt, &resp);
          return resp;
        }) {
    chirp::notification::DeviceRegistration reg;
    reg.device_id = "dev";
    reg.user_id = "u";
    reg.platform = "android";
    service.RegisterDevice(reg);
  }

  uint16_t port() const { return server.port(); }

  chirp::notification::NotificationService service;
  chirp::notification::NotificationHandlers handlers{service};
  FakeNotificationServer server;
};

}  // namespace

class NotificationClientTest : public ::testing::Test {};

TEST_F(NotificationClientTest, PushRoundTripDeliversResponse) {
  Loopback loop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", loop.port());

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  req.set_title("t");
  req.set_body("b");

  std::promise<chirp::notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, /*seq=*/9,
                   [&promise](const chirp::notification::PushNotificationResponse& resp) {
                     promise.set_value(resp);
                   });

  ASSERT_TRUE(SpinIoFor(io, future));
  const auto resp = future.get();
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_GT(resp.server_time(), 0);
}

TEST_F(NotificationClientTest, DeviceRpcRoundTrips) {
  Loopback loop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", loop.port());

  // Register a second device, update its token, list, then unregister.
  chirp::notification::RegisterDeviceRequest reg;
  reg.set_user_id("u");
  reg.set_device_id("dev-2");
  reg.set_platform("ios");
  reg.set_apns_token("tok-1");
  {
    std::promise<chirp::notification::RegisterDeviceResponse> p;
    auto f = p.get_future();
    client.AsyncRegisterDevice(reg, 1, [&p](const chirp::notification::RegisterDeviceResponse& r) {
      p.set_value(r);
    });
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::OK);
  }

  chirp::notification::UpdateDeviceTokenRequest tok;
  tok.set_device_id("dev-2");
  tok.set_apns_token("tok-2");
  {
    std::promise<chirp::notification::UpdateDeviceTokenResponse> p;
    auto f = p.get_future();
    client.AsyncUpdateDeviceToken(tok, 2,
                                  [&p](const chirp::notification::UpdateDeviceTokenResponse& r) {
                                    p.set_value(r);
                                  });
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::OK);
  }
  EXPECT_EQ(loop.service.GetUserDevices("u").size(), 2u);

  chirp::notification::GetUserDevicesRequest list;
  list.set_user_id("u");
  {
    std::promise<chirp::notification::GetUserDevicesResponse> p;
    auto f = p.get_future();
    client.AsyncGetUserDevices(list, 3,
                               [&p](const chirp::notification::GetUserDevicesResponse& r) {
                                 p.set_value(r);
                               });
    ASSERT_TRUE(SpinIoFor(io, f));
    const auto resp = f.get();
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.devices_size(), 2);
  }

  chirp::notification::UnregisterDeviceRequest un;
  un.set_user_id("u");
  un.set_device_id("dev-2");
  {
    std::promise<chirp::notification::UnregisterDeviceResponse> p;
    auto f = p.get_future();
    client.AsyncUnregisterDevice(un, 4,
                                 [&p](const chirp::notification::UnregisterDeviceResponse& r) {
                                   p.set_value(r);
                                 });
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::OK);
  }
  EXPECT_EQ(loop.service.GetUserDevices("u").size(), 1u);
}

TEST_F(NotificationClientTest, RefusedEndpointReportsInternalError) {
  asio::io_context io;
  NotificationClient client(io, kRefusedHost, kRefusedPort);

  chirp::notification::PushNotificationRequest push;
  push.set_user_id("u");
  chirp::notification::RegisterDeviceRequest reg;
  reg.set_user_id("u");
  reg.set_device_id("d");
  chirp::notification::UnregisterDeviceRequest un;
  un.set_device_id("d");
  chirp::notification::UpdateDeviceTokenRequest tok;
  tok.set_device_id("d");
  tok.set_fcm_token("t");
  chirp::notification::GetUserDevicesRequest list;
  list.set_user_id("u");

  {
    std::promise<chirp::notification::PushNotificationResponse> p1;
    auto f1 = p1.get_future();
    client.AsyncPush(push, 1, [&p1](const chirp::notification::PushNotificationResponse& r) {
      p1.set_value(r);
    });
    ASSERT_TRUE(SpinIoFor(io, f1));
    EXPECT_EQ(f1.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::notification::RegisterDeviceResponse> p2;
    auto f2 = p2.get_future();
    client.AsyncRegisterDevice(reg, 2,
                               [&p2](const chirp::notification::RegisterDeviceResponse& r) {
                                 p2.set_value(r);
                               });
    ASSERT_TRUE(SpinIoFor(io, f2));
    EXPECT_EQ(f2.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::notification::UnregisterDeviceResponse> p3;
    auto f3 = p3.get_future();
    client.AsyncUnregisterDevice(un, 3,
                                 [&p3](const chirp::notification::UnregisterDeviceResponse& r) {
                                   p3.set_value(r);
                                 });
    ASSERT_TRUE(SpinIoFor(io, f3));
    EXPECT_EQ(f3.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::notification::UpdateDeviceTokenResponse> p4;
    auto f4 = p4.get_future();
    client.AsyncUpdateDeviceToken(tok, 4,
                                  [&p4](const chirp::notification::UpdateDeviceTokenResponse& r) {
                                    p4.set_value(r);
                                  });
    ASSERT_TRUE(SpinIoFor(io, f4));
    EXPECT_EQ(f4.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::notification::GetUserDevicesResponse> p5;
    auto f5 = p5.get_future();
    client.AsyncGetUserDevices(list, 5,
                               [&p5](const chirp::notification::GetUserDevicesResponse& r) {
                                 p5.set_value(r);
                               });
    ASSERT_TRUE(SpinIoFor(io, f5));
    EXPECT_EQ(f5.get().code(), chirp::common::INTERNAL_ERROR);
  }
}

TEST_F(NotificationClientTest, GarbageResponseBodyReportsInternalError) {
  // The server answers with the paired msg id but a non-proto body.
  FakeNotificationServer fake([](const chirp::gateway::Packet& pkt) {
    (void)pkt;
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::PUSH_NOTIFICATION_RESP);
    out.set_body("not-proto");
    return out;
  });
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", fake.port());

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, DroppedConnectionReportsInternalError) {
  DroppingNotificationServer drop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", drop.port());

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, WrongResponseMsgIdReportsInternalError) {
  FakeNotificationServer fake([](const chirp::gateway::Packet& pkt) {
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::GET_USER_DEVICES_RESP);  // wrong pairing
    out.set_sequence(pkt.sequence());
    out.set_body(chirp::notification::PushNotificationResponse().SerializeAsString());
    return out;
  });
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", fake.port());

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, DrainedThenDroppedClientIsSafe) {
  asio::io_context io;
  NotificationClient client(io, kRefusedHost, kRefusedPort);
  chirp::notification::DrainAndDropNotificationClientForTest(client);
  // The helper is idempotent, and the destructor hits its null-impl guard.
  chirp::notification::DrainAndDropNotificationClientForTest(client);
}

TEST_F(NotificationClientTest, NullCallbackIsTolerated) {
  Loopback loop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", loop.port());

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  client.AsyncPush(req, 1, nullptr);

  // The worker must complete the RPC and stop cleanly on destruction.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

TEST_F(NotificationClientTest, DestructorDrainsQueuedJobs) {
  asio::io_context io;
  NotificationClient client(io, kRefusedHost, kRefusedPort);

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::array<std::promise<chirp::notification::PushNotificationResponse>, 3> promises;
  for (int i = 0; i < 3; i++) {
    auto& p = promises[i];
    client.AsyncPush(req, i,
                     [&p](const chirp::notification::PushNotificationResponse& r) {
                       p.set_value(r);
                     });
  }

  // Destroy immediately: the queued jobs are drained by the worker before
  // join, and each callback lands on the io with INTERNAL_ERROR.
  {
    NotificationClient drained(io, kRefusedHost, kRefusedPort);
    drained.AsyncPush(req, 99, [](const chirp::notification::PushNotificationResponse&) {});
  }

  for (auto& p : promises) {
    auto f = p.get_future();
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::INTERNAL_ERROR);
  }
}
