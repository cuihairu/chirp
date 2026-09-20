// Unit tests for the app gateway edge (services/app_gateway/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions. Device-message forwarding runs through a real
// NotificationClient against a loopback FakeNotificationServer, pinning the
// authenticated-user pinning and response relay behavior.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "network/session_registry.h"
#include "network/notification_client.h"
#include "network/server_gateway_peer.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/notification.pb.h"
#include "proto/server_gateway.pb.h"

// Relative path on purpose: a bare "main.cc" would resolve through the -I
// path to services/gateway/src/main.cc (the game gateway) instead.
#define main chirp_app_gateway_main
#include "../../services/app_gateway/src/main.cc"
#undef main

namespace {

using chirp::gateway::Packet;

// Pure in-memory Session mock: records everything sent through it.
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

Packet MakePacket(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
template <typename T>
bool DecodeFramed(const std::string& framed, T* out) {
  if (framed.size() < 4u) {
    return false;
  }
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  if (framed.size() != 4u + static_cast<size_t>(len)) {
    return false;
  }
  return out->ParseFromString(framed.substr(4, len));
}

std::vector<Packet> ReceivedPackets(const MockSession& s) {
  std::vector<Packet> out;
  for (const auto& framed : s.sent) {
    Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      out.push_back(pkt);
    }
  }
  return out;
}

// Parses the most recent frame as a Packet and its body as T.
template <typename T>
bool LastBody(const MockSession& s, T* out) {
  if (s.sent.empty()) {
    return false;
  }
  Packet pkt;
  if (!DecodeFramed(s.sent.back(), &pkt)) {
    return false;
  }
  return out->ParseFromString(pkt.body());
}

// Loopback stand-in for the notification service: answers every packet with
// an OK response of the matching RESP id and records what arrived.
class FakeNotificationServer {
 public:
  FakeNotificationServer()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
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

  // Snapshots the request bodies received so far (raw Packet bodies).
  std::vector<std::string> Received() {
    std::lock_guard<std::mutex> lock(mu_);
    return received_;
  }

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
        {
          std::lock_guard<std::mutex> lock(mu_);
          received_.push_back(pkt.body());
        }

        chirp::gateway::MsgID resp_id = chirp::gateway::REGISTER_DEVICE_RESP;
        if (pkt.msg_id() == chirp::gateway::UNREGISTER_DEVICE_REQ) {
          resp_id = chirp::gateway::UNREGISTER_DEVICE_RESP;
        } else if (pkt.msg_id() == chirp::gateway::UPDATE_DEVICE_TOKEN_REQ) {
          resp_id = chirp::gateway::UPDATE_DEVICE_TOKEN_RESP;
        } else if (pkt.msg_id() == chirp::gateway::GET_USER_DEVICES_REQ) {
          resp_id = chirp::gateway::GET_USER_DEVICES_RESP;
        }

        chirp::gateway::Packet resp;
        resp.set_msg_id(resp_id);
        resp.set_sequence(pkt.sequence());
        // Every device response leads with the same wire shape (code = 1),
        // but build the right type for clarity.
        std::string body;
        switch (pkt.msg_id()) {
        case chirp::gateway::UNREGISTER_DEVICE_REQ: {
          chirp::notification::UnregisterDeviceResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        case chirp::gateway::UPDATE_DEVICE_TOKEN_REQ: {
          chirp::notification::UpdateDeviceTokenResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        case chirp::gateway::GET_USER_DEVICES_REQ: {
          chirp::notification::GetUserDevicesResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        default: {
          chirp::notification::RegisterDeviceResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        }
        resp.set_body(body);
        auto framed = chirp::network::ProtobufFraming::Encode(resp);
        asio::error_code ec;
        asio::write(*sock, asio::buffer(framed), ec);
      }
      partial_ -= 4 + len;
      std::memmove(buf_.data(), buf_.data() + 4 + len, partial_);
    }
  }

  std::mutex mu_;
  std::vector<std::string> received_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::thread thread_;
  std::array<char, 65536> buf_{};
  size_t partial_{0};
};

// Loopback stand-in for the server-plane hub: completes the service auth
// handshake, pongs heartbeats, answers the three subscription RPCs with a
// scripted OK (plus a minted subscription id), and records every request
// body it received.
class FakeServerGatewayServer {
 public:
  FakeServerGatewayServer()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeServerGatewayServer() {
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

  // Snapshots the request bodies received so far (raw Packet bodies).
  std::vector<std::string> Received() {
    std::lock_guard<std::mutex> lock(mu_);
    return received_;
  }

  size_t CountAuth() {
    std::lock_guard<std::mutex> lock(mu_);
    return auth_count_;
  }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      socket_ = sock;
      DoRead(sock);
      DoAccept();
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
        {
          std::lock_guard<std::mutex> lock(mu_);
          received_.push_back(pkt.body());
          if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
            auth_count_++;
          }
        }

        chirp::gateway::Packet resp;
        resp.set_sequence(pkt.sequence());
        bool answered = true;
        switch (pkt.msg_id()) {
        case chirp::gateway::SERVER_AUTH_REQ: {
          chirp::server_gateway::ServerAuthResponse r;
          r.set_code(chirp::common::OK);
          r.set_heartbeat_interval_seconds(1);
          resp.set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::SERVER_HEARTBEAT_PING: {
          chirp::server_gateway::ServerHeartbeatPong r;
          resp.set_msg_id(chirp::gateway::SERVER_HEARTBEAT_PONG);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ: {
          chirp::server_gateway::SubscribePlayerChannelResponse r;
          r.set_code(chirp::common::OK);
          r.set_subscription_id("sub-fake-1");
          resp.set_msg_id(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ: {
          chirp::server_gateway::UnsubscribePlayerChannelResponse r;
          r.set_code(chirp::common::OK);
          resp.set_msg_id(chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ: {
          chirp::server_gateway::GetPlayerSubscriptionsResponse r;
          r.set_code(chirp::common::OK);
          r.add_subscriptions()->set_subscription_id("sub-fake-1");
          resp.set_msg_id(chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        default:
          answered = false;  // nothing else is answered
          break;
        }
        if (answered) {
          auto framed = chirp::network::ProtobufFraming::Encode(resp);
          asio::error_code ec;
          asio::write(*sock, asio::buffer(framed), ec);
        }
      }
      partial_ -= 4 + len;
      std::memmove(buf_.data(), buf_.data() + 4 + len, partial_);
    }
  }

  std::mutex mu_;
  std::vector<std::string> received_;
  size_t auth_count_{0};
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::thread thread_;
  std::array<char, 65536> buf_{};
  size_t partial_{0};
};

// Polls `pred` with the peer-side io_context pumped on every attempt: the
// hub fake runs on its own thread, but the peer's strand lives on `io`.
template <typename Pred>
bool WaitForIo(asio::io_context& io, Pred pred, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    io.poll();
    io.restart();
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  io.poll();
  io.restart();
  return pred();
}

class AppGatewayServiceTest : public ::testing::Test {
 protected:
  std::shared_ptr<chirp::network::SessionRegistry> state_ =
      std::make_shared<chirp::network::SessionRegistry>();
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();

  // auth=null: the scaffold login path (no auth service configured).
  void SendFrame(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
                 chirp::network::ServerGatewayPeer* sg = nullptr) {
    HandleClientPacket(session_, MakePacket(id, seq, body).SerializeAsString(),
                       state_, nullptr, nullptr, nullptr, sg);
  }

  // Scaffold-login `user` on `session` and return the assigned session_id.
  std::string Login(const std::shared_ptr<MockSession>& session, const std::string& user) {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    HandleClientPacket(session, MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()).SerializeAsString(),
                       state_, nullptr, nullptr, nullptr, nullptr);
    chirp::auth::LoginResponse resp;
    EXPECT_FALSE(session->sent.empty());
    if (!session->sent.empty()) {
      EXPECT_TRUE(LastBody(*session, &resp));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    return resp.session_id();
  }
};

TEST_F(AppGatewayServiceTest, ScaffoldLoginEmptyTokenRejected) {
  chirp::auth::LoginRequest req;
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // Nothing bound.
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(AppGatewayServiceTest, ScaffoldLoginBindsSession) {
  const std::string sid = Login(session_, "alice");
  EXPECT_FALSE(sid.empty());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.user_id(), "alice");
  EXPECT_TRUE(resp.kick_previous());
  EXPECT_EQ(resp.kick().reason(), "login from another device");

  auto authed = chirp::network::GetAuthenticatedSession(state_, session_);
  EXPECT_EQ(authed.user_id, "alice");
  EXPECT_EQ(authed.session_id, sid);
}

TEST_F(AppGatewayServiceTest, ReLoginKicksPreviousSession) {
  auto s1 = std::make_shared<MockSession>();
  Login(s1, "alice");

  auto s2 = std::make_shared<MockSession>();
  Login(s2, "alice");

  // s1 must have received the kick frame and been closed after send.
  bool kicked = false;
  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      chirp::auth::KickNotify kick;
      ASSERT_TRUE(kick.ParseFromString(pkt.body()));
      EXPECT_EQ(kick.reason(), "login from another device");
      kicked = true;
    }
  }
  EXPECT_TRUE(kicked);
  EXPECT_TRUE(s1->close_after_send);

  // The registry now points at s2.
  auto authed = chirp::network::GetAuthenticatedSession(state_, s2);
  EXPECT_EQ(authed.user_id, "alice");

  // The kicked session keeps its registration until the connection actually
  // closes; disconnect is what releases it.
  HandleDisconnect(s1, state_, nullptr);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, s1).user_id.empty());
  // s2 must be untouched by s1's disconnect.
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, s2).user_id, "alice");
}

TEST_F(AppGatewayServiceTest, LogoutEmptyUserRejected) {
  chirp::auth::LogoutRequest req;
  SendFrame(chirp::gateway::LOGOUT_REQ, 2, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(session_->close_after_send);
}

TEST_F(AppGatewayServiceTest, LogoutUnboundSessionDenied) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  SendFrame(chirp::gateway::LOGOUT_REQ, 2, req.SerializeAsString());

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(AppGatewayServiceTest, LogoutUserMismatchDenied) {
  Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("bob");
  SendFrame(chirp::gateway::LOGOUT_REQ, 2, req.SerializeAsString());

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  // Still bound.
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).user_id, "alice");
}

TEST_F(AppGatewayServiceTest, LogoutSessionIdMismatchDenied) {
  Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  req.set_session_id("stale-session-id");
  SendFrame(chirp::gateway::LOGOUT_REQ, 2, req.SerializeAsString());

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SESSION_EXPIRED);
}

TEST_F(AppGatewayServiceTest, LogoutHappyPathClosesAndUnbinds) {
  const std::string sid = Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  req.set_session_id(sid);
  SendFrame(chirp::gateway::LOGOUT_REQ, 3, req.SerializeAsString());

  ASSERT_FALSE(session_->sent.empty());
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(session_->close_after_send);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(AppGatewayServiceTest, HeartbeatPongEchoesTimestampAndSequence) {
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(424242);
  SendFrame(chirp::gateway::HEARTBEAT_PING, 88, ping.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session_->sent.back(), &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  EXPECT_EQ(pkt.sequence(), 88);
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(pong.ParseFromString(pkt.body()));
  EXPECT_EQ(pong.timestamp(), 424242);
}

TEST_F(AppGatewayServiceTest, ChatBusinessPacketIgnored) {
  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 5, "");
  EXPECT_TRUE(session_->sent.empty());
}

TEST_F(AppGatewayServiceTest, RegisterDeviceGarbageBodyRejected) {
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, "\x01\x02\x03");

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceUnauthenticatedDenied) {
  chirp::notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, req.SerializeAsString());

  chirp::notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceWithoutNotificationPlaneUnavailable) {
  Login(session_, "alice");

  chirp::notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, req.SerializeAsString());

  chirp::notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceForwardedWithPinnedUserId) {
  FakeNotificationServer fake;
  asio::io_context io;
  auto notification = std::make_unique<chirp::notification::NotificationClient>(
      io, "127.0.0.1", fake.port());

  Login(session_, "alice");

  chirp::notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  req.set_user_id("spoofed-user");  // must be overwritten by the gateway
  req.set_platform("android");

  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::REGISTER_DEVICE_REQ, 9, req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr);

  // The response delivery is posted to the main io_context; pump until the
  // relay lands on the session.
  for (int i = 0; i < 500 && session_->sent.empty(); i++) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  DrainAndDropNotificationClientForTest(*notification);
  notification.reset();

  // The notification plane must have received the authenticated user.
  auto received = fake.Received();
  ASSERT_EQ(received.size(), 1u);
  chirp::notification::RegisterDeviceRequest forwarded;
  ASSERT_TRUE(forwarded.ParseFromString(received[0]));
  EXPECT_EQ(forwarded.user_id(), "alice");
  EXPECT_EQ(forwarded.device_id(), "dev-1");

  // And the OK response must have been relayed to the session.
  ASSERT_FALSE(session_->sent.empty());
  chirp::notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(AppGatewayServiceTest, UpdateDeviceTokenForwardedByDeviceIdOnly) {
  FakeNotificationServer fake;
  asio::io_context io;
  auto notification = std::make_unique<chirp::notification::NotificationClient>(
      io, "127.0.0.1", fake.port());

  Login(session_, "alice");

  chirp::notification::UpdateDeviceTokenRequest req;
  req.set_device_id("dev-9");
  req.set_fcm_token("fcm-token-xyz");

  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, 10, req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr);

  for (int i = 0; i < 500 && session_->sent.empty(); i++) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  DrainAndDropNotificationClientForTest(*notification);
  notification.reset();

  auto received = fake.Received();
  ASSERT_EQ(received.size(), 1u);
  chirp::notification::UpdateDeviceTokenRequest forwarded;
  ASSERT_TRUE(forwarded.ParseFromString(received[0]));
  EXPECT_EQ(forwarded.device_id(), "dev-9");
  EXPECT_EQ(forwarded.fcm_token(), "fcm-token-xyz");
  // No user_id field at all: the update message addresses devices by id.

  ASSERT_FALSE(session_->sent.empty());
  chirp::notification::UpdateDeviceTokenResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(AppGatewayServiceTest, SubscriptionUnauthenticatedDenied) {
  chirp::server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("alice");
  req.set_game_id("game_a");
  req.set_channel_id("world");
  SendFrame(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11, req.SerializeAsString());

  chirp::server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(AppGatewayServiceTest, SubscriptionWithoutServerPlaneUnavailable) {
  Login(session_, "alice");

  chirp::server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("alice");
  req.set_game_id("game_a");
  req.set_channel_id("world");
  SendFrame(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11, req.SerializeAsString());

  chirp::server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
}

TEST_F(AppGatewayServiceTest, SubscriptionForwardedWithPinnedPlayerId) {
  FakeServerGatewayServer fake;
  asio::io_context io;
  chirp::network::ServerGatewayPeer::Options opts;
  opts.host = "127.0.0.1";
  opts.port = fake.port();
  opts.service_id = "app_gateway";
  opts.secret = "edge-secret";
  opts.reconnect_delay_seconds = 1;
  auto sg = chirp::network::ServerGatewayPeer::Create(io, opts, nullptr, nullptr);
  sg->Start();

  Login(session_, "alice");

  // Wait for the service auth handshake to complete on both sides. The fake
  // writes SERVER_AUTH_RESP on its own thread; the peer strand only advances
  // on poll, so keep pumping past the fake's auth count before forwarding.
  ASSERT_TRUE(WaitForIo(io, [&] { return fake.CountAuth() >= 1; }, std::chrono::seconds(5)));
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("mallory");  // must be overwritten with the authenticated user
  req.set_game_id("game_a");
  req.set_channel_id("world");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get());

  // The hub's OK response (with the minted id) must reach the session. Wait
  // on the specific response frame, not on sent.empty(): the login response
  // is already sitting there, and the RPC only moves while `io` is pumped.
  const auto got_subscription_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP && p.sequence() == 11) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_subscription_resp, std::chrono::seconds(5)));

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));

  // The hub saw the authenticated user, not the spoofed one. Scan rather
  // than take the last frame: heartbeats are recorded too.
  bool found = false;
  chirp::server_gateway::SubscribePlayerChannelRequest forwarded;
  for (const auto& body : fake.Received()) {
    chirp::server_gateway::SubscribePlayerChannelRequest candidate;
    // The game_id filter keeps a heartbeat ping's wire bytes (which share
    // field 1) from parsing into a match.
    if (candidate.ParseFromString(body) && candidate.game_id() == "game_a") {
      forwarded = candidate;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  EXPECT_EQ(forwarded.player_id(), "alice");
  EXPECT_EQ(forwarded.channel_id(), "world");

  ASSERT_FALSE(session_->sent.empty());
  chirp::server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.subscription_id(), "sub-fake-1");
}

TEST_F(AppGatewayServiceTest, DisconnectUnbindsSession) {
  Login(session_, "alice");

  HandleDisconnect(session_, state_, nullptr);

  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());

  // A second disconnect is a no-op (no crash, no double release).
  HandleDisconnect(session_, state_, nullptr);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(AppGatewayServiceTest, DisconnectUnboundSessionIsNoop) {
  auto s = std::make_shared<MockSession>();
  HandleDisconnect(s, state_, nullptr);
  EXPECT_TRUE(s->sent.empty());
}

}  // namespace
