#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <string>

#include <asio.hpp>

#include "auth_client.h"
#include "fake_servers.h"
#include "network/redis_client.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "redis_session_manager.h"

using chirp::gateway::AuthClient;
using chirp::gateway::RedisSessionManager;
using chirp::auth::LoginRequest;
using chirp::auth::LoginResponse;
using chirp::auth::LogoutRequest;
using chirp::auth::LogoutResponse;

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

class AuthClientTest : public ::testing::Test {};

TEST_F(AuthClientTest, AsyncLoginConnectFailureReportsInternalError) {
  asio::io_context io;
  AuthClient client(io, kRefusedHost, kRefusedPort);

  std::promise<LoginResponse> promise;
  auto future = promise.get_future();
  LoginRequest req;
  req.set_token("tok");
  client.AsyncLogin(req, /*seq=*/1,
                    [&promise](const LoginResponse& resp) { promise.set_value(resp); });

  // Worker posts the callback onto the main io_context once it fails.
  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthClientTest, AsyncLogoutConnectFailureReportsInternalError) {
  asio::io_context io;
  AuthClient client(io, kRefusedHost, kRefusedPort);

  std::promise<LogoutResponse> promise;
  auto future = promise.get_future();
  LogoutRequest req;
  req.set_user_id("u1");
  client.AsyncLogout(req, /*seq=*/2,
                     [&promise](const LogoutResponse& resp) { promise.set_value(resp); });

  ASSERT_TRUE(SpinIoFor(io, future));
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthClientTest, MultipleJobsProcessedBeforeDestruction) {
  asio::io_context io;
  {
    AuthClient client(io, kRefusedHost, kRefusedPort);

    std::atomic<int> responses{0};
    for (int i = 0; i < 5; ++i) {
      LoginRequest req;
      req.set_token("t" + std::to_string(i));
      client.AsyncLogin(req, i, [&](const LoginResponse&) { responses++; });
      LogoutRequest out;
      out.set_user_id("u" + std::to_string(i));
      client.AsyncLogout(out, i + 100, [&](const LogoutResponse&) { responses++; });
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline && responses.load() < 10) {
      io.poll();
      io.restart();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GE(responses.load(), 1);
  }
  SUCCEED();  // destructor joined the worker cleanly
}

TEST_F(AuthClientTest, DestroyWithoutPendingJobsIsClean) {
  asio::io_context io;
  { AuthClient client(io, kRefusedHost, kRefusedPort); }
  SUCCEED();
}

class RedisSessionManagerTest : public ::testing::Test {};

namespace {

// A Redis client double whose every command throws: exercises the worker's
// exception paths (both claim and release jobs).
class ThrowingRedisClient : public chirp::network::RedisClient {
 public:
  ThrowingRedisClient() : RedisClient("127.0.0.1", 1) {}
  std::optional<std::string> Get(const std::string&) override {
    throw std::runtime_error("get failed");
  }
  bool SetEx(const std::string&, const std::string&, int) override {
    throw std::runtime_error("setex failed");
  }
  bool Del(const std::string&) override {
    throw std::runtime_error("del failed");
  }
  bool Publish(const std::string&, const std::string&) override {
    throw std::runtime_error("publish failed");
  }
};

// A double that throws a non-std exception for the catch-all branch.
struct WeirdException {};
class WeirdThrowingRedisClient : public ThrowingRedisClient {
 public:
  std::optional<std::string> Get(const std::string&) override { throw WeirdException{}; }
};

}  // namespace

TEST_F(RedisSessionManagerTest, ClaimWithThrowingRedisDeliversEmptyOwner) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  {
    RedisSessionManager mgr(io, "127.0.0.1", 1, "inst-1", 60, nullptr,
                            [] { return std::unique_ptr<chirp::network::RedisClient>(std::make_unique<ThrowingRedisClient>()); });

    std::promise<std::optional<std::string>> promise;
    auto future = promise.get_future();
    mgr.AsyncClaim("alice", [&promise](std::optional<std::string> prev) {
      promise.set_value(prev);
    });
    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(3000)), std::future_status::ready);
    EXPECT_FALSE(future.get().has_value());

    // Release path with a throwing client must not crash either.
    mgr.AsyncRelease("alice");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  io.stop();
  runner.join();
}

TEST_F(RedisSessionManagerTest, ClaimWithNonStdExceptionAlsoDeliversEmptyOwner) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  {
    RedisSessionManager mgr(io, "127.0.0.1", 1, "inst-2", 60, nullptr,
                            [] { return std::unique_ptr<chirp::network::RedisClient>(std::make_unique<WeirdThrowingRedisClient>()); });
    std::promise<std::optional<std::string>> promise;
    auto future = promise.get_future();
    mgr.AsyncClaim("bob", [&promise](std::optional<std::string> prev) {
      promise.set_value(prev);
    });
    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(3000)), std::future_status::ready);
    EXPECT_FALSE(future.get().has_value());
  }

  io.stop();
  runner.join();
}

TEST_F(RedisSessionManagerTest, ClaimWithoutRedisDeliversEmptyOwner) {
  asio::io_context io;
  std::atomic<int> kicks{0};
  RedisSessionManager mgr(io, kRefusedHost, kRefusedPort, "inst-1", 60,
                          [&kicks](const std::string&) { kicks++; });
  EXPECT_EQ(mgr.InstanceId(), "inst-1");

  std::promise<std::optional<std::string>> promise;
  auto future = promise.get_future();
  mgr.AsyncClaim("alice",
                 [&promise](std::optional<std::string> prev) { promise.set_value(prev); });

  ASSERT_TRUE(SpinIoFor(io, future));
  // No previous owner could be read (Redis unreachable).
  EXPECT_FALSE(future.get().has_value());
  EXPECT_EQ(kicks.load(), 0);
}

TEST_F(RedisSessionManagerTest, ClaimCallbackMayBeNull) {
  asio::io_context io;
  RedisSessionManager mgr(io, kRefusedHost, kRefusedPort, "inst-2", 60, nullptr);
  mgr.AsyncClaim("bob", nullptr);  // null callback must not crash
  io.run_for(std::chrono::milliseconds(1000));
  SUCCEED();
}

TEST_F(RedisSessionManagerTest, ReleaseWithoutRedisIsSafe) {
  asio::io_context io;
  RedisSessionManager mgr(io, kRefusedHost, kRefusedPort, "inst-3", 60,
                          [](const std::string&) {});
  mgr.AsyncRelease("carol");
  io.run_for(std::chrono::milliseconds(1000));
  SUCCEED();
}

TEST_F(RedisSessionManagerTest, JobsEnqueuedBeforeStopAreDrained) {
  asio::io_context io;
  std::atomic<int> claims{0};
  {
    RedisSessionManager mgr(io, kRefusedHost, kRefusedPort, "inst-4", 60,
                            [](const std::string&) {});
    for (int i = 0; i < 3; ++i) {
      mgr.AsyncClaim("u" + std::to_string(i),
                     [&](std::optional<std::string>) { claims++; });
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(2000);
    while (std::chrono::steady_clock::now() < deadline && claims.load() < 3) {
      io.poll();
      io.restart();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_GE(claims.load(), 1);
  }  // Stop() must join worker without hanging
  SUCCEED();
}

}  // namespace

// ---------------------------------------------------------------------------
// Loopback fake auth server: length-prefixed protobuf packets, answers
// LOGIN_REQ / LOGOUT_REQ so the real AuthClient success paths are covered.
// ---------------------------------------------------------------------------

#include <cstring>

#include "network/protobuf_framing.h"
#include "proto/gateway.pb.h"

namespace {

class FakeAuthServer {
 public:
  using Handler = std::function<chirp::gateway::Packet(const chirp::gateway::Packet&)>;

  explicit FakeAuthServer(Handler handler)
      : handler_(std::move(handler)),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeAuthServer() {
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
      DoRead();
    });
  }

  void DoRead() {
    auto sock = socket_;
    sock->async_read_some(asio::buffer(buf_.data() + partial_, buf_.size() - partial_),
                          [this, sock](const std::error_code& ec, std::size_t n) {
                            if (ec) {
                              return;
                            }
                            partial_ += n;
                            Consume();
                            if (sock->is_open()) {
                              DoRead();
                            }
                          });
  }

  void Consume() {
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
        asio::write(*socket_, asio::buffer(framed), ec);
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

class AuthClientLoopbackTest : public ::testing::Test {};

TEST_F(AuthClientLoopbackTest, LoginSuccessDeliversResponse) {
  FakeAuthServer fake([](const chirp::gateway::Packet& pkt) {
    chirp::auth::LoginResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_user_id("user-42");
    resp.set_session_id("sess");
    resp.set_server_time(1234567890);  // echoed through the parse
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::LOGIN_RESP);
    out.set_sequence(pkt.sequence());
    out.set_body(resp.SerializeAsString());
    return out;
  });

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LoginResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LoginRequest req;
  req.set_token("tok");
  client.AsyncLogin(req, 7,
                    [&promise](const chirp::auth::LoginResponse& r) { promise.set_value(r); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  const auto resp = future.get();
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user-42");
  EXPECT_EQ(resp.server_time(), 1234567890);  // response value wins the parse
}

TEST_F(AuthClientLoopbackTest, LoginWithWrongMsgIdFails) {
  FakeAuthServer fake([](const chirp::gateway::Packet& pkt) {
    // Wrong msg_id -> client must map this to INTERNAL_ERROR
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::KICK_NOTIFY);
    out.set_sequence(pkt.sequence());
    out.set_body("junk");
    return out;
  });

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LoginResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LoginRequest req;
  client.AsyncLogin(req, 1,
                    [&promise](const chirp::auth::LoginResponse& r) { promise.set_value(r); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthClientLoopbackTest, LogoutSuccessDeliversResponse) {
  FakeAuthServer fake([](const chirp::gateway::Packet& pkt) {
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::LOGOUT_RESP);
    out.set_sequence(pkt.sequence());
    out.set_body(resp.SerializeAsString());
    return out;
  });

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LogoutResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LogoutRequest req;
  req.set_user_id("u1");
  client.AsyncLogout(req, 3,
                     [&promise](const chirp::auth::LogoutResponse& r) { promise.set_value(r); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().code(), chirp::common::OK);
}

}  // namespace


// ---------------------------------------------------------------------------
// RedisSessionManager against the fake Redis (claim/kick/release flows).
// ---------------------------------------------------------------------------

class SessionManagerKickTest : public ::testing::Test {};

TEST_F(SessionManagerKickTest, ClaimKicksPreviousOwner) {
  std::mutex mu;
  std::vector<std::vector<std::string>> cmds;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mu);
    cmds.push_back(args);
    if (!args.empty() && args[0] == "GET") {
      // The session is currently owned by another instance.
      return chirp_test::Bulk("other-inst");
    }
    if (!args.empty() && args[0] == "PUBLISH") {
      return chirp_test::Int(1);
    }
    return chirp_test::Simple("OK");
  });

  asio::io_context io;
  RedisSessionManager mgr(io, "127.0.0.1", fake.port(), "my-inst", 60,
                          [](const std::string&) {});

  std::promise<std::optional<std::string>> promise;
  auto future = promise.get_future();
  mgr.AsyncClaim("alice",
                 [&promise](std::optional<std::string> prev) { promise.set_value(prev); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().value_or(""), "other-inst");

  // The manager must have published a kick for the previous owner.
  bool kicked = false;
  {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& c : cmds) {
      if (c.size() >= 3 && c[0] == "PUBLISH" && c[1] == "chirp:kick:other-inst" &&
          c[2] == "alice") {
        kicked = true;
      }
    }
  }
  EXPECT_TRUE(kicked);
}

TEST_F(SessionManagerKickTest, ClaimKeepsOwnSessionWithoutKick) {
  std::mutex mu;
  std::atomic<int> publishes{0};
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mu);
    if (!args.empty() && args[0] == "GET") {
      return chirp_test::Bulk("my-inst");  // we already own it
    }
    if (!args.empty() && args[0] == "PUBLISH") {
      ++publishes;
    }
    return chirp_test::Simple("OK");
  });

  asio::io_context io;
  RedisSessionManager mgr(io, "127.0.0.1", fake.port(), "my-inst", 60,
                          [](const std::string&) {});

  std::promise<std::optional<std::string>> promise;
  auto future = promise.get_future();
  mgr.AsyncClaim("alice",
                 [&promise](std::optional<std::string> prev) { promise.set_value(prev); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().value_or(""), "my-inst");
  EXPECT_EQ(publishes.load(), 0);
}

TEST_F(SessionManagerKickTest, KickSubscriptionDeliversCallback) {
  std::mutex mu;
  std::vector<std::string> subscribed;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mu);
    if (!args.empty() && (args[0] == "SUBSCRIBE" || args[0] == "UNSUBSCRIBE")) {
      if (args.size() > 1) subscribed.push_back(args[1]);
    }
    if (!args.empty() && args[0] == "GET") {
      return chirp_test::Bulk("other-inst");
    }
    return chirp_test::Simple("OK");
  });

  asio::io_context io;
  std::promise<std::string> kick_promise;
  auto kick_future = kick_promise.get_future();
  RedisSessionManager mgr(io, "127.0.0.1", fake.port(), "inst-1", 60,
                          [&kick_promise](const std::string& user) {
                            kick_promise.set_value(user);
                          });

  // Wait until the manager subscribed to its kick channel.
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!subscribed.empty()) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::lock_guard<std::mutex> lock(mu);
    ASSERT_FALSE(subscribed.empty());
    EXPECT_EQ(subscribed[0], "chirp:kick:inst-1");
  }

  fake.Publish("chirp:kick:inst-1", "bob");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         kick_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(kick_future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(kick_future.get(), "bob");
}

TEST_F(SessionManagerKickTest, ReleaseDeletesOwnSessionOnly) {
  std::mutex mu;
  std::vector<std::string> deleted;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lock(mu);
    if (!args.empty() && args[0] == "GET") {
      return chirp_test::Bulk("my-inst");
    }
    if (!args.empty() && args[0] == "DEL") {
      deleted.push_back(args.size() > 1 ? args[1] : "");
    }
    return chirp_test::Simple("OK");
  });

  asio::io_context io;
  RedisSessionManager mgr(io, "127.0.0.1", fake.port(), "my-inst", 60,
                          [](const std::string&) {});
  mgr.AsyncRelease("alice");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  std::lock_guard<std::mutex> lock(mu);
  ASSERT_EQ(deleted.size(), 1u);
  EXPECT_EQ(deleted[0], "chirp:sess:alice");
}

// ---------------------------------------------------------------------------
// Auth client error paths: wrong logout msg id, dropped connection, garbage
// response frames.
// ---------------------------------------------------------------------------

namespace {

void PumpForResponse(asio::io_context& io, std::future<chirp::auth::LoginResponse>& future) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void PumpForLogoutResponse(asio::io_context& io, std::future<chirp::auth::LogoutResponse>& future) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

// Accepts one connection and closes it right away.
class DroppingAuthServer {
 public:
  DroppingAuthServer()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }
  ~DroppingAuthServer() {
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

// Accepts one connection and writes a framed garbage payload.
class GarbageAuthServer {
 public:
  explicit GarbageAuthServer(std::string bytes)
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)),
        reply_(std::move(bytes)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }
  ~GarbageAuthServer() {
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
      // Swallow whatever the client sends, then answer with garbage.
      auto drain = std::make_shared<std::array<char, 512>>();
      sock->async_read_some(asio::buffer(*drain),
                            [this, sock, drain](const std::error_code&, std::size_t) {
                              asio::error_code wec;
                              asio::write(*sock, asio::buffer(reply_), wec);
                            });
    });
  }
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::string reply_;
  std::thread thread_;
};

std::string FramedGarbage() {
  const std::string junk = "\xde\xad\xbe\xef junk";
  const uint32_t len = static_cast<uint32_t>(junk.size());
  std::string out;
  out.push_back(static_cast<char>((len >> 24) & 0xFF));
  out.push_back(static_cast<char>((len >> 16) & 0xFF));
  out.push_back(static_cast<char>((len >> 8) & 0xFF));
  out.push_back(static_cast<char>(len & 0xFF));
  out += junk;
  return out;
}

}  // namespace

TEST_F(AuthClientLoopbackTest, LogoutWithWrongMsgIdYieldsInternalError) {
  FakeAuthServer fake([](const chirp::gateway::Packet& pkt) {
    // Answer a logout request with a LOGIN_RESP: the client must reject it.
    chirp::gateway::Packet out;
    out.set_msg_id(chirp::gateway::LOGIN_RESP);
    out.set_sequence(pkt.sequence());
    out.set_body("junk");
    return out;
  });

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LogoutResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LogoutRequest req;
  client.AsyncLogout(req, 1,
                     [&promise](const chirp::auth::LogoutResponse& r) { promise.set_value(r); });

  PumpForLogoutResponse(io, future);
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthClientLoopbackTest, ServerDropsConnectionLoginFails) {
  DroppingAuthServer fake;

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LoginResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LoginRequest req;
  client.AsyncLogin(req, 1,
                    [&promise](const chirp::auth::LoginResponse& r) { promise.set_value(r); });

  PumpForResponse(io, future);
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthClientLoopbackTest, ServerRepliesGarbageFrameLoginFails) {
  GarbageAuthServer fake(FramedGarbage());

  asio::io_context io;
  AuthClient client(io, "127.0.0.1", fake.port());

  std::promise<chirp::auth::LoginResponse> promise;
  auto future = promise.get_future();
  chirp::auth::LoginRequest req;
  client.AsyncLogin(req, 1,
                    [&promise](const chirp::auth::LoginResponse& r) { promise.set_value(r); });

  PumpForResponse(io, future);
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
  EXPECT_EQ(future.get().code(), chirp::common::INTERNAL_ERROR);
}
