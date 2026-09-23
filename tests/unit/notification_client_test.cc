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
#include "network/notification_client.h"
#include "notification_handlers.h"
#include "notification_service.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "push_bridge.h"

using chirp::app_notification::NotificationClient;

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
    chirp::app_notification::DeviceRegistration reg;
    reg.device_id = "dev";
    reg.user_id = "u";
    reg.platform = "android";
    reg.fcm_token = "tok";  // untokened devices fail closed now
    service.RegisterDevice(reg);
  }

  uint16_t port() const { return server.port(); }

  // Provider requests are answered inline; the client under test only
  // needs the service to report success.
  struct OkTransport : chirp::app_notification::PushTransport {
    std::string Post(const chirp::app_notification::PushRequest&) override {
      return "{}";
    }
  };
  chirp::app_notification::NotificationService service{
      chirp::app_notification::FCMConfig{}, chirp::app_notification::APNsConfig{},
      std::make_shared<OkTransport>()};
  chirp::app_notification::NotificationHandlers handlers{service};
  FakeNotificationServer server;
};

// Answers push requests with a canned OK while recording the parsed request
// bodies, so tests can assert what a producer actually sent.
struct RecordingPushServer {
  RecordingPushServer() : server([this](const chirp::gateway::Packet& pkt) {
    if (pkt.msg_id() == chirp::gateway::PUSH_NOTIFICATION_REQ) {
      chirp::app_notification::PushNotificationRequest req;
      if (req.ParseFromString(pkt.body())) {
        std::lock_guard<std::mutex> lock(mu);
        pushes.push_back(std::move(req));
      }
    }
    chirp::app_notification::PushNotificationResponse body;
    body.set_code(chirp::common::OK);
    body.set_server_time(1);
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::PUSH_NOTIFICATION_RESP);
    out.set_sequence(pkt.sequence());
    out.set_body(body.SerializeAsString());
    return out;
  }) {}

  uint16_t port() const { return server.port(); }

  std::mutex mu;
  std::vector<chirp::app_notification::PushNotificationRequest> pushes;
  FakeNotificationServer server;
};

chirp::chat::ChatMessage MakeChatMessage(const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id("m-1");
  msg.set_sender_id("bob");
  msg.set_receiver_id("alice");
  msg.set_channel_type(chirp::chat::PRIVATE);
  msg.set_channel_id("alice|bob");
  msg.set_content(content);
  return msg;
}

}  // namespace

// --- PushBridge ---------------------------------------------------------------

TEST(PushBridgeTest, NullClientIsNoOp) {
  chirp::chat::PushBridge bridge(nullptr);
  bridge.NotifyOffline(MakeChatMessage("hi"), "alice");
  bridge.NotifyOffline(MakeChatMessage("hi"), "");  // empty user is skipped too
}

TEST(PushBridgeTest, PayloadCarriesMessageMetadata) {
  RecordingPushServer server;
  asio::io_context io;
  chirp::chat::PushBridge bridge(
      std::make_shared<NotificationClient>(io, "127.0.0.1", server.port()));

  bridge.NotifyOffline(MakeChatMessage("hello there"), "alice");

  // Wait outside the mutex: holding mu across the sleep can starve the
  // server thread's own lock in the handler, delaying the push past the
  // whole deadline (observed as a 3s flake).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool done = false;
  while (std::chrono::steady_clock::now() < deadline && !done) {
    io.poll();
    io.restart();
    {
      std::lock_guard<std::mutex> lock(server.mu);
      done = !server.pushes.empty();
    }
    if (!done) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  std::lock_guard<std::mutex> lock(server.mu);
  ASSERT_EQ(server.pushes.size(), 1u);
  const auto& req = server.pushes[0];
  EXPECT_EQ(req.user_id(), "alice");
  EXPECT_EQ(req.title(), "bob");
  EXPECT_EQ(req.body(), "hello there");
  EXPECT_EQ(req.tag(), "alice|bob");
  EXPECT_EQ(req.click_action(), "chirp://chat/alice|bob");
  EXPECT_EQ(req.data().at("type"), "message");
  EXPECT_EQ(req.data().at("from_user_id"), "bob");
  EXPECT_EQ(req.data().at("channel_id"), "alice|bob");
  EXPECT_EQ(req.data().at("channel_type"), "private");
  EXPECT_EQ(req.data().at("message_id"), "m-1");
}

TEST(PushBridgeTest, LongBodyIsTruncated) {
  RecordingPushServer server;
  asio::io_context io;
  chirp::chat::PushBridge bridge(
      std::make_shared<NotificationClient>(io, "127.0.0.1", server.port()));

  bridge.NotifyOffline(MakeChatMessage(std::string(150, 'x')), "alice");

  // Same lock-free wait as above: never sleep while holding server.mu.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool done = false;
  while (std::chrono::steady_clock::now() < deadline && !done) {
    io.poll();
    io.restart();
    {
      std::lock_guard<std::mutex> lock(server.mu);
      done = !server.pushes.empty();
    }
    if (!done) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  std::lock_guard<std::mutex> lock(server.mu);
  ASSERT_EQ(server.pushes.size(), 1u);
  EXPECT_EQ(server.pushes[0].body(), std::string(100, 'x') + "...");
}

TEST(PushBridgeTest, ChannelTypeIsNamedPerChannelKind) {
  const std::vector<std::pair<chirp::chat::ChannelType, std::string>> cases = {
      {chirp::chat::TEAM, "team"},
      {chirp::chat::GUILD, "guild"},
      {chirp::chat::WORLD, "world"},
      {chirp::chat::PRIVATE, "private"},
  };
  for (const auto& [type, name] : cases) {
    RecordingPushServer server;
    asio::io_context io;
    chirp::chat::PushBridge bridge(
        std::make_shared<NotificationClient>(io, "127.0.0.1", server.port()));

    chirp::chat::ChatMessage msg = MakeChatMessage("hi");
    msg.set_channel_type(type);
    bridge.NotifyOffline(msg, "alice");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool done = false;
    while (std::chrono::steady_clock::now() < deadline && !done) {
      io.poll();
      io.restart();
      {
        std::lock_guard<std::mutex> lock(server.mu);
        done = !server.pushes.empty();
      }
      if (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    std::lock_guard<std::mutex> lock(server.mu);
    ASSERT_EQ(server.pushes.size(), 1u) << static_cast<int>(type);
    EXPECT_EQ(server.pushes[0].data().at("channel_type"), name) << static_cast<int>(type);
  }
}

TEST(PushBridgeTest, RefusedEndpointDoesNotAffectCaller) {
  asio::io_context io;
  chirp::chat::PushBridge bridge(
      std::make_shared<NotificationClient>(io, kRefusedHost, kRefusedPort));

  bridge.NotifyOffline(MakeChatMessage("hi"), "alice");

  // Just spin: the failure is reported to nobody and must not throw.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  while (std::chrono::steady_clock::now() < deadline) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

class NotificationClientTest : public ::testing::Test {};

TEST_F(NotificationClientTest, PushRoundTripDeliversResponse) {
  Loopback loop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", loop.port());

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  req.set_title("t");
  req.set_body("b");

  std::promise<chirp::app_notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, /*seq=*/9,
                   [&promise](const chirp::app_notification::PushNotificationResponse& resp) {
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
  chirp::app_notification::RegisterDeviceRequest reg;
  reg.set_user_id("u");
  reg.set_device_id("dev-2");
  reg.set_platform("ios");
  reg.set_apns_token("tok-1");
  {
    std::promise<chirp::app_notification::RegisterDeviceResponse> p;
    auto f = p.get_future();
    client.AsyncRegisterDevice(reg, 1, [&p](const chirp::app_notification::RegisterDeviceResponse& r) {
      p.set_value(r);
    });
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::OK);
  }

  chirp::app_notification::UpdateDeviceTokenRequest tok;
  tok.set_device_id("dev-2");
  tok.set_apns_token("tok-2");
  {
    std::promise<chirp::app_notification::UpdateDeviceTokenResponse> p;
    auto f = p.get_future();
    client.AsyncUpdateDeviceToken(tok, 2,
                                  [&p](const chirp::app_notification::UpdateDeviceTokenResponse& r) {
                                    p.set_value(r);
                                  });
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::OK);
  }
  EXPECT_EQ(loop.service.GetUserDevices("u").size(), 2u);

  chirp::app_notification::GetUserDevicesRequest list;
  list.set_user_id("u");
  {
    std::promise<chirp::app_notification::GetUserDevicesResponse> p;
    auto f = p.get_future();
    client.AsyncGetUserDevices(list, 3,
                               [&p](const chirp::app_notification::GetUserDevicesResponse& r) {
                                 p.set_value(r);
                               });
    ASSERT_TRUE(SpinIoFor(io, f));
    const auto resp = f.get();
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.devices_size(), 2);
  }

  chirp::app_notification::UnregisterDeviceRequest un;
  un.set_user_id("u");
  un.set_device_id("dev-2");
  {
    std::promise<chirp::app_notification::UnregisterDeviceResponse> p;
    auto f = p.get_future();
    client.AsyncUnregisterDevice(un, 4,
                                 [&p](const chirp::app_notification::UnregisterDeviceResponse& r) {
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

  chirp::app_notification::PushNotificationRequest push;
  push.set_user_id("u");
  chirp::app_notification::RegisterDeviceRequest reg;
  reg.set_user_id("u");
  reg.set_device_id("d");
  chirp::app_notification::UnregisterDeviceRequest un;
  un.set_device_id("d");
  chirp::app_notification::UpdateDeviceTokenRequest tok;
  tok.set_device_id("d");
  tok.set_fcm_token("t");
  chirp::app_notification::GetUserDevicesRequest list;
  list.set_user_id("u");

  {
    std::promise<chirp::app_notification::PushNotificationResponse> p1;
    auto f1 = p1.get_future();
    client.AsyncPush(push, 1, [&p1](const chirp::app_notification::PushNotificationResponse& r) {
      p1.set_value(r);
    });
    ASSERT_TRUE(SpinIoFor(io, f1));
    EXPECT_EQ(f1.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::app_notification::RegisterDeviceResponse> p2;
    auto f2 = p2.get_future();
    client.AsyncRegisterDevice(reg, 2,
                               [&p2](const chirp::app_notification::RegisterDeviceResponse& r) {
                                 p2.set_value(r);
                               });
    ASSERT_TRUE(SpinIoFor(io, f2));
    EXPECT_EQ(f2.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::app_notification::UnregisterDeviceResponse> p3;
    auto f3 = p3.get_future();
    client.AsyncUnregisterDevice(un, 3,
                                 [&p3](const chirp::app_notification::UnregisterDeviceResponse& r) {
                                   p3.set_value(r);
                                 });
    ASSERT_TRUE(SpinIoFor(io, f3));
    EXPECT_EQ(f3.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::app_notification::UpdateDeviceTokenResponse> p4;
    auto f4 = p4.get_future();
    client.AsyncUpdateDeviceToken(tok, 4,
                                  [&p4](const chirp::app_notification::UpdateDeviceTokenResponse& r) {
                                    p4.set_value(r);
                                  });
    ASSERT_TRUE(SpinIoFor(io, f4));
    EXPECT_EQ(f4.get().code(), chirp::common::INTERNAL_ERROR);

    std::promise<chirp::app_notification::GetUserDevicesResponse> p5;
    auto f5 = p5.get_future();
    client.AsyncGetUserDevices(list, 5,
                               [&p5](const chirp::app_notification::GetUserDevicesResponse& r) {
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

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::app_notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::app_notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, UnparseableOuterFrameReportsInternalError) {
  // ReadFrame succeeds (4-byte BE length + payload) but the payload is not a
  // gateway.Packet: ParseFromArray fails before the msg_id check.
  class RawGarbageServer {
   public:
    RawGarbageServer()
        : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
      port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
      acceptor_.async_accept([this](const std::error_code& ec,
                                    asio::ip::tcp::socket sock) {
        if (ec) return;
        const std::string junk = "\xff\xff\xff\xff\x01\x02";
        std::string frame;
        frame.push_back(0);
        frame.push_back(0);
        frame.push_back(0);
        frame.push_back(static_cast<char>(junk.size()));
        frame.append(junk);
        asio::error_code wec;
        asio::write(sock, asio::buffer(frame), wec);
        sock.close(wec);
      });
      thread_ = std::thread([this] { io_.run(); });
    }
    ~RawGarbageServer() {
      io_.stop();
      if (thread_.joinable()) thread_.join();
    }
    uint16_t port() const { return port_; }

   private:
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    uint16_t port_{0};
    std::thread thread_;
  };
  RawGarbageServer raw;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", raw.port());

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::app_notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::app_notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, DroppedConnectionReportsInternalError) {
  DroppingNotificationServer drop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", drop.port());

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::app_notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::app_notification::PushNotificationResponse& r) {
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
    out.set_body(chirp::app_notification::PushNotificationResponse().SerializeAsString());
    return out;
  });
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", fake.port());

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::promise<chirp::app_notification::PushNotificationResponse> promise;
  auto future = promise.get_future();
  client.AsyncPush(req, 1, [&promise](const chirp::app_notification::PushNotificationResponse& r) {
    promise.set_value(r);
  });
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(NotificationClientTest, DrainedThenDroppedClientIsSafe) {
  asio::io_context io;
  NotificationClient client(io, kRefusedHost, kRefusedPort);
  chirp::app_notification::DrainAndDropNotificationClientForTest(client);
  // The helper is idempotent, and the destructor hits its null-impl guard.
  chirp::app_notification::DrainAndDropNotificationClientForTest(client);
}

TEST_F(NotificationClientTest, NullCallbackIsTolerated) {
  Loopback loop;
  asio::io_context io;
  NotificationClient client(io, "127.0.0.1", loop.port());

  chirp::app_notification::PushNotificationRequest req;
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

  chirp::app_notification::PushNotificationRequest req;
  req.set_user_id("u");
  std::array<std::promise<chirp::app_notification::PushNotificationResponse>, 3> promises;
  for (int i = 0; i < 3; i++) {
    auto& p = promises[i];
    client.AsyncPush(req, i,
                     [&p](const chirp::app_notification::PushNotificationResponse& r) {
                       p.set_value(r);
                     });
  }

  // Destroy immediately: the queued jobs are drained by the worker before
  // join, and each callback lands on the io with INTERNAL_ERROR.
  {
    NotificationClient drained(io, kRefusedHost, kRefusedPort);
    drained.AsyncPush(req, 99, [](const chirp::app_notification::PushNotificationResponse&) {});
  }

  for (auto& p : promises) {
    auto f = p.get_future();
    ASSERT_TRUE(SpinIoFor(io, f));
    EXPECT_EQ(f.get().code(), chirp::common::INTERNAL_ERROR);
  }
}
