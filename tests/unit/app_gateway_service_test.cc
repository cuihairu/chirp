// Unit tests for the app gateway edge (services/app/sdk_gateway/src/main.cc).
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
#include "fake_chat_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/app_notification.pb.h"
#include "proto/game_server_gateway.pb.h"

// Relative path on purpose: a bare "main.cc" would resolve through the -I
// path to services/game/sdk_gateway/src/main.cc (the game gateway) instead.
#define main chirp_app_gateway_main
#include "../../services/app/sdk_gateway/src/main.cc"
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
          chirp::app_notification::UnregisterDeviceResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        case chirp::gateway::UPDATE_DEVICE_TOKEN_REQ: {
          chirp::app_notification::UpdateDeviceTokenResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        case chirp::gateway::GET_USER_DEVICES_REQ: {
          chirp::app_notification::GetUserDevicesResponse r;
          r.set_code(chirp::common::OK);
          body = r.SerializeAsString();
          break;
        }
        default: {
          chirp::app_notification::RegisterDeviceResponse r;
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
          chirp::game_server_gateway::ServerAuthResponse r;
          r.set_code(chirp::common::OK);
          r.set_heartbeat_interval_seconds(1);
          resp.set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::SERVER_HEARTBEAT_PING: {
          chirp::game_server_gateway::ServerHeartbeatPong r;
          resp.set_msg_id(chirp::gateway::SERVER_HEARTBEAT_PONG);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ: {
          chirp::game_server_gateway::SubscribePlayerChannelResponse r;
          r.set_code(chirp::common::OK);
          r.set_subscription_id("sub-fake-1");
          resp.set_msg_id(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ: {
          chirp::game_server_gateway::UnsubscribePlayerChannelResponse r;
          r.set_code(chirp::common::OK);
          resp.set_msg_id(chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ: {
          chirp::game_server_gateway::GetPlayerSubscriptionsResponse r;
          r.set_code(chirp::common::OK);
          r.add_subscriptions()->set_subscription_id("sub-fake-1");
          resp.set_msg_id(chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::MARK_CHANNELS_READ_REQ: {
          chirp::game_server_gateway::MarkChannelsReadResponse r;
          r.set_code(chirp::common::OK);
          r.set_cleared(1);
          resp.set_msg_id(chirp::gateway::MARK_CHANNELS_READ_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::GET_UNREAD_SUMMARY_REQ: {
          chirp::game_server_gateway::GetUnreadSummaryResponse r;
          r.set_code(chirp::common::OK);
          auto* entry = r.add_entries();
          entry->set_game_id("game_a");
          entry->set_channel_id("world");
          entry->set_unread_count(2);
          r.set_total_unread(2);
          resp.set_msg_id(chirp::gateway::GET_UNREAD_SUMMARY_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::SET_GAME_PRESENCE_ENABLED_REQ: {
          chirp::game_server_gateway::SetGamePresenceEnabledResponse r;
          r.set_code(chirp::common::OK);
          resp.set_msg_id(chirp::gateway::SET_GAME_PRESENCE_ENABLED_RESP);
          resp.set_body(r.SerializeAsString());
          break;
        }
        case chirp::gateway::GET_GAME_PRESENCE_REQ: {
          chirp::game_server_gateway::GetGamePresenceResponse r;
          r.set_code(chirp::common::OK);
          r.set_enabled(true);
          auto* entry = r.add_entries();
          entry->set_game_id("game_a");
          entry->set_game_user_id("u-1");
          resp.set_msg_id(chirp::gateway::GET_GAME_PRESENCE_RESP);
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
  // Lives on the fixture so chat-bridge tests can pump it through WaitForIo;
  // the bridge is created on demand via AttachChatBridge().
  asio::io_context io_;
  std::unique_ptr<chirp::gateway::ChatBridge> bridge_;

  // Points the edge's chat pipeline at a loopback fake chat server. The
  // handshake result codes are scriptable (default both OK); the returned
  // server outlives the bridge (destructor order in the fixture).
  chirp_test::FakeChatServer& AttachChatBridge(
      chirp::common::ErrorCode auth_code = chirp::common::OK,
      chirp::common::ErrorCode login_code = chirp::common::OK) {
    chat_ = std::make_unique<chirp_test::FakeChatServer>(auth_code, login_code);
    bridge_ = std::make_unique<chirp::gateway::ChatBridge>(io_, "127.0.0.1", chat_->port(),
                                                           "app_gateway", "edge-secret");
    return *chat_;
  }

  // auth=null: the scaffold login path (no auth service configured).
  void SendFrame(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
                 chirp::network::ServerGatewayPeer* sg = nullptr,
                 chirp::gateway::ChatBridge* bridge = nullptr) {
    HandleClientPacket(session_, MakePacket(id, seq, body).SerializeAsString(),
                       state_, nullptr, nullptr, nullptr, sg, bridge);
  }

  // Scaffold-login `user` on `session` and return the assigned session_id.
  std::string Login(const std::shared_ptr<MockSession>& session, const std::string& user,
                    chirp::gateway::ChatBridge* bridge = nullptr) {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    HandleClientPacket(session, MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()).SerializeAsString(),
                       state_, nullptr, nullptr, nullptr, nullptr, bridge);
    chirp::auth::LoginResponse resp;
    EXPECT_FALSE(session->sent.empty());
    if (!session->sent.empty()) {
      EXPECT_TRUE(LastBody(*session, &resp));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    return resp.session_id();
  }

  // Destruction order matters and is declaration-reverse: chat_ (the
  // listener) dies first, closing the bridge's sockets while io_ is still
  // alive; bridge_ then drops its pending handlers; io_ last.
  std::unique_ptr<chirp_test::FakeChatServer> chat_;
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
  EXPECT_EQ(resp.kick().reason(), "logged in on another device");

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
      EXPECT_EQ(kick.reason(), "logged in on another device");
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
  HandleDisconnect(s1, state_, nullptr, nullptr);
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

TEST_F(AppGatewayServiceTest, ChatBusinessPacketIgnoredWithoutBridge) {
  // No --chat_host: the edge keeps the pre-bridge behavior, 2xxx ignored.
  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 5, "");
  EXPECT_TRUE(session_->sent.empty());
}

TEST_F(AppGatewayServiceTest, ChatBusinessPacketIgnoredWhenUnauthenticated) {
  AttachChatBridge();
  // Bridge configured but the client never logged in: still ignored, and
  // nothing reaches chat (the pipeline only carries authenticated traffic).
  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 5, "", nullptr, bridge_.get());
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_EQ(chat_->Count(chirp::gateway::SEND_MESSAGE_REQ), 0u);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceGarbageBodyRejected) {
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, "\x01\x02\x03");

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::app_notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceUnauthenticatedDenied) {
  chirp::app_notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, req.SerializeAsString());

  chirp::app_notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceWithoutNotificationPlaneUnavailable) {
  Login(session_, "alice");

  chirp::app_notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  SendFrame(chirp::gateway::REGISTER_DEVICE_REQ, 6, req.SerializeAsString());

  chirp::app_notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
}

TEST_F(AppGatewayServiceTest, RegisterDeviceForwardedWithPinnedUserId) {
  FakeNotificationServer fake;
  asio::io_context io;
  auto notification = std::make_unique<chirp::app_notification::NotificationClient>(
      io, "127.0.0.1", fake.port());

  Login(session_, "alice");

  chirp::app_notification::RegisterDeviceRequest req;
  req.set_device_id("dev-1");
  req.set_user_id("spoofed-user");  // must be overwritten by the gateway
  req.set_platform("android");

  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::REGISTER_DEVICE_REQ, 9, req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr, nullptr);

  // The response delivery is posted to the main io_context; pump until the
  // REGISTER_DEVICE_RESP frame lands. Waiting on sent.empty() is wrong here:
  // the login response is already sitting in sent, so the loop would exit
  // immediately and the relay assertions below would pass vacuously (found
  // by coverage - the client callback at the SendPacket relay never ran).
  const auto got_register_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::REGISTER_DEVICE_RESP && p.sequence() == 9) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_register_resp, std::chrono::seconds(5)));
  DrainAndDropNotificationClientForTest(*notification);
  notification.reset();

  // The notification plane must have received the authenticated user.
  auto received = fake.Received();
  ASSERT_EQ(received.size(), 1u);
  chirp::app_notification::RegisterDeviceRequest forwarded;
  ASSERT_TRUE(forwarded.ParseFromString(received[0]));
  EXPECT_EQ(forwarded.user_id(), "alice");
  EXPECT_EQ(forwarded.device_id(), "dev-1");

  // And the OK response must have been relayed to the session.
  ASSERT_FALSE(session_->sent.empty());
  chirp::app_notification::RegisterDeviceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(AppGatewayServiceTest, UpdateDeviceTokenForwardedByDeviceIdOnly) {
  FakeNotificationServer fake;
  asio::io_context io;
  auto notification = std::make_unique<chirp::app_notification::NotificationClient>(
      io, "127.0.0.1", fake.port());

  Login(session_, "alice");

  chirp::app_notification::UpdateDeviceTokenRequest req;
  req.set_device_id("dev-9");
  req.set_fcm_token("fcm-token-xyz");

  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, 10, req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr, nullptr);

  // Same specific-frame wait as the register test: sent.empty() is already
  // false from the login response.
  const auto got_update_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::UPDATE_DEVICE_TOKEN_RESP && p.sequence() == 10) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_update_resp, std::chrono::seconds(5)));
  DrainAndDropNotificationClientForTest(*notification);
  notification.reset();

  auto received = fake.Received();
  ASSERT_EQ(received.size(), 1u);
  chirp::app_notification::UpdateDeviceTokenRequest forwarded;
  ASSERT_TRUE(forwarded.ParseFromString(received[0]));
  EXPECT_EQ(forwarded.device_id(), "dev-9");
  EXPECT_EQ(forwarded.fcm_token(), "fcm-token-xyz");
  // No user_id field at all: the update message addresses devices by id.

  ASSERT_FALSE(session_->sent.empty());
  chirp::app_notification::UpdateDeviceTokenResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(AppGatewayServiceTest, SubscriptionUnauthenticatedDenied) {
  chirp::game_server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("alice");
  req.set_game_id("game_a");
  req.set_channel_id("world");
  SendFrame(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11, req.SerializeAsString());

  chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(AppGatewayServiceTest, SubscriptionWithoutServerPlaneUnavailable) {
  Login(session_, "alice");

  chirp::game_server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("alice");
  req.set_game_id("game_a");
  req.set_channel_id("world");
  SendFrame(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11, req.SerializeAsString());

  chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
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

  chirp::game_server_gateway::SubscribePlayerChannelRequest req;
  req.set_player_id("mallory");  // must be overwritten with the authenticated user
  req.set_game_id("game_a");
  req.set_channel_id("world");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 11,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);

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
  chirp::game_server_gateway::SubscribePlayerChannelRequest forwarded;
  for (const auto& body : fake.Received()) {
    chirp::game_server_gateway::SubscribePlayerChannelRequest candidate;
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
  chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.subscription_id(), "sub-fake-1");
}

TEST_F(AppGatewayServiceTest, GamePresenceForwardedWithPinnedPlayerId) {
  // The 游戏在线状态 pair rides the same self-service path as the WP-8
  // block: player_id is pinned to the authenticated user, so no dial can
  // flip or read another account's switch.
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
  ASSERT_TRUE(WaitForIo(io, [&] { return fake.CountAuth() >= 1; }, std::chrono::seconds(5)));
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::game_server_gateway::SetGamePresenceEnabledRequest set_req;
  set_req.set_player_id("mallory");  // must be overwritten with the authenticated user
  set_req.set_enabled(false);
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::SET_GAME_PRESENCE_ENABLED_REQ, 21,
                                set_req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);
  // Wait on the specific response frame, not on sent.empty(): the login
  // response is already sitting there (the same trap the subscription test
  // documents), and frames arrive length-prefixed so they must be decoded.
  const auto got_set_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::SET_GAME_PRESENCE_ENABLED_RESP && p.sequence() == 21) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_set_resp, std::chrono::seconds(5)));
  chirp::game_server_gateway::SetGamePresenceEnabledResponse set_resp;
  ASSERT_TRUE(LastBody(*session_, &set_resp));
  EXPECT_EQ(set_resp.code(), chirp::common::OK);

  chirp::game_server_gateway::GetGamePresenceRequest get_req;
  get_req.set_player_id("mallory");
  session_->sent.clear();
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::GET_GAME_PRESENCE_REQ, 22,
                                get_req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);
  const auto got_presence = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::GET_GAME_PRESENCE_RESP && p.sequence() == 22) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_presence, std::chrono::seconds(5)));

  // The hub's body travels verbatim: enabled + the bound game come back.
  chirp::game_server_gateway::GetGamePresenceResponse get_resp;
  ASSERT_TRUE(LastBody(*session_, &get_resp));
  EXPECT_EQ(get_resp.code(), chirp::common::OK);
  EXPECT_TRUE(get_resp.enabled());
  ASSERT_EQ(get_resp.entries_size(), 1);
  EXPECT_EQ(get_resp.entries(0).game_id(), "game_a");

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));

  // No frame that ever left the edge named another player. Scanning the
  // whole recording (heartbeats included) is the honest assertion here: the
  // two request types share field 1 with other messages, so pinning is
  // checked by absence of the spoofed id rather than by type match.
  for (const auto& body : fake.Received()) {
    EXPECT_EQ(body.find("mallory"), std::string::npos)
        << "spoofed player_id leaked to the hub in " << body.size() << " bytes";
  }
  bool saw_alice = false;
  for (const auto& body : fake.Received()) {
    chirp::game_server_gateway::SetGamePresenceEnabledRequest candidate;
    if (candidate.ParseFromString(body) && candidate.player_id() == "alice") {
      saw_alice = true;
    }
  }
  EXPECT_TRUE(saw_alice) << "the pinned request never reached the hub";
}

TEST_F(AppGatewayServiceTest, UnreadMarkForwardedWithPinnedPlayerId) {
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
  ASSERT_TRUE(WaitForIo(io, [&] { return fake.CountAuth() >= 1; }, std::chrono::seconds(5)));
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::game_server_gateway::MarkChannelsReadRequest req;
  req.set_player_id("mallory");  // must be overwritten with the authenticated user
  req.set_game_id("game_a");
  req.set_channel_id("world");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::MARK_CHANNELS_READ_REQ, 12,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);

  const auto got_mark_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::MARK_CHANNELS_READ_RESP && p.sequence() == 12) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_mark_resp, std::chrono::seconds(5)));

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));

  // The hub saw the authenticated user, not the spoofed one. The game_id
  // filter keeps a heartbeat ping's wire bytes (which share field 1) from
  // parsing into a match.
  bool found = false;
  chirp::game_server_gateway::MarkChannelsReadRequest forwarded;
  for (const auto& body : fake.Received()) {
    chirp::game_server_gateway::MarkChannelsReadRequest candidate;
    if (candidate.ParseFromString(body) && candidate.game_id() == "game_a") {
      forwarded = candidate;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  EXPECT_EQ(forwarded.player_id(), "alice");
  EXPECT_EQ(forwarded.channel_id(), "world");

  chirp::game_server_gateway::MarkChannelsReadResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.cleared(), 1);
}

TEST_F(AppGatewayServiceTest, UnreadSummaryForwardedAndRelayed) {
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
  ASSERT_TRUE(WaitForIo(io, [&] { return fake.CountAuth() >= 1; }, std::chrono::seconds(5)));
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::game_server_gateway::GetUnreadSummaryRequest req;
  req.set_player_id("mallory");  // must be overwritten with the authenticated user
  req.set_game_id("game_a");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::GET_UNREAD_SUMMARY_REQ, 13,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);

  const auto got_summary_resp = [&] {
    for (const auto& framed : session_->sent) {
      Packet p;
      if (DecodeFramed(framed, &p) &&
          p.msg_id() == chirp::gateway::GET_UNREAD_SUMMARY_RESP && p.sequence() == 13) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io, got_summary_resp, std::chrono::seconds(5)));

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));

  bool found = false;
  chirp::game_server_gateway::GetUnreadSummaryRequest forwarded;
  for (const auto& body : fake.Received()) {
    chirp::game_server_gateway::GetUnreadSummaryRequest candidate;
    if (candidate.ParseFromString(body) && candidate.game_id() == "game_a") {
      forwarded = candidate;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  EXPECT_EQ(forwarded.player_id(), "alice");

  // The hub's body (entries + total) is relayed verbatim.
  chirp::game_server_gateway::GetUnreadSummaryResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  ASSERT_EQ(resp.entries_size(), 1);
  EXPECT_EQ(resp.entries(0).channel_id(), "world");
  EXPECT_EQ(resp.entries(0).unread_count(), 2);
  EXPECT_EQ(resp.total_unread(), 2);
}

TEST_F(AppGatewayServiceTest, DisconnectUnbindsSession) {
  Login(session_, "alice");

  HandleDisconnect(session_, state_, nullptr, nullptr);

  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());

  // A second disconnect is a no-op (no crash, no double release).
  HandleDisconnect(session_, state_, nullptr, nullptr);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(AppGatewayServiceTest, DisconnectUnboundSessionIsNoop) {
  auto s = std::make_shared<MockSession>();
  HandleDisconnect(s, state_, nullptr, nullptr);
  EXPECT_TRUE(s->sent.empty());
}

// ---- Chat pipeline (ChatBridge) ----
//
// The edge relays chat business packets (2xxx) through a per-client internal
// connection to chirp_chat once the client is authenticated. The fake chat
// runs on its own io thread; the bridge lives on the fixture's io_, pumped
// by WaitForIo - the same loopback shape as chat_bridge_test.cc.

TEST_F(AppGatewayServiceTest, ChatHandshakeReplaysLoginWithServiceAuth) {
  auto& chat = AttachChatBridge();
  Login(session_, "alice", bridge_.get());

  // The pipeline authenticates itself as a trusted service first, then
  // replays the client's login (token + device passthrough).
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                        std::chrono::seconds(5)));
  const auto auths = chat.All(chirp::gateway::SERVER_AUTH_REQ);
  ASSERT_FALSE(auths.empty());
  chirp::game_server_gateway::ServerAuthRequest auth_req;
  ASSERT_TRUE(auth_req.ParseFromString(auths.front().body()));
  EXPECT_EQ(auth_req.service_id(), "app_gateway");
  EXPECT_EQ(auth_req.secret(), "edge-secret");

  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));
  const auto logins = chat.All(chirp::gateway::LOGIN_REQ);
  ASSERT_FALSE(logins.empty());
  chirp::auth::LoginRequest login_req;
  ASSERT_TRUE(login_req.ParseFromString(logins.front().body()));
  EXPECT_EQ(login_req.token(), "alice");
  // Verbatim passthrough: the raw device_id field, not the normalized one
  // the edge uses for its own session binding.
  EXPECT_EQ(login_req.device_id(), "");
}

TEST_F(AppGatewayServiceTest, ChatBusinessPacketForwardedVerbatimWhenReady) {
  auto& chat = AttachChatBridge();
  Login(session_, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  // Identity intact: msg_id, sequence and body survive the relay untouched.
  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 42, "body-bytes", nullptr, bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) > 0; },
                        std::chrono::seconds(5)));
  const auto sent = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  ASSERT_FALSE(sent.empty());
  EXPECT_EQ(sent.back().sequence(), 42);
  EXPECT_EQ(sent.back().body(), "body-bytes");
}

TEST_F(AppGatewayServiceTest, ChatPushRelayedBackToClient) {
  auto& chat = AttachChatBridge();
  Login(session_, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  // Downlink: chat pushes on the internal connection and the frame lands on
  // the one client, untouched. Handshake replies are consumed by the bridge,
  // never relayed.
  chirp::chat::ChatMessage msg;
  msg.set_sender_id("user_2");
  msg.set_content("hello from chat");
  chat.SendToLatest(chirp_test::MakePacket(chirp::gateway::CHAT_MESSAGE_NOTIFY, 99, msg));

  const auto has_notify = [&] {
    for (const auto& f : ReceivedPackets(*session_)) {
      // The service-auth reply is consumed inside the bridge, never relayed
      // (LOGIN_RESP here is the edge's own frame, so it shares msg_id 1004
      // and is expected).
      EXPECT_NE(f.msg_id(), chirp::gateway::SERVER_AUTH_RESP);
      if (f.msg_id() == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io_, has_notify, std::chrono::seconds(5)));
  const auto frames = ReceivedPackets(*session_);
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::CHAT_MESSAGE_NOTIFY);
  EXPECT_EQ(frames.back().sequence(), 99);
  chirp::chat::ChatMessage got;
  ASSERT_TRUE(got.ParseFromString(frames.back().body()));
  EXPECT_EQ(got.content(), "hello from chat");
}

TEST_F(AppGatewayServiceTest, ChatHandshakeFailureKicksClient) {
  AttachChatBridge(chirp::common::AUTH_FAILED);
  Login(session_, "alice", bridge_.get());

  // A rejected service-auth kicks the real client so it reconnects cleanly.
  const auto has_kick = [&] {
    for (const auto& f : ReceivedPackets(*session_)) {
      if (f.msg_id() == chirp::gateway::KICK_NOTIFY) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io_, has_kick, std::chrono::seconds(5)));
  const auto frames = ReceivedPackets(*session_);
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(frames.back().body()));
  EXPECT_EQ(kick.reason(), "chat unavailable");
  EXPECT_TRUE(session_->close_after_send);
  // Deliberately not asserted: the handshake fires SERVER_AUTH_REQ and the
  // LOGIN_REQ replay back to back (the replay must not wait an extra RTT),
  // so the fake may consume both before the AUTH_FAILED reply closes the
  // pipe. The contract is the KICK above: a rejected service auth never
  // relays chat frames to the client.
}

TEST_F(AppGatewayServiceTest, ChatPipeLostKicksClient) {
  auto& chat = AttachChatBridge();
  Login(session_, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  chat.CloseLatest();
  const auto has_kick = [&] {
    for (const auto& f : ReceivedPackets(*session_)) {
      if (f.msg_id() == chirp::gateway::KICK_NOTIFY) {
        return true;
      }
    }
    return false;
  };
  ASSERT_TRUE(WaitForIo(io_, has_kick, std::chrono::seconds(5)));
  const auto frames = ReceivedPackets(*session_);
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(frames.back().body()));
  EXPECT_EQ(kick.reason(), "chat session lost");
}

TEST_F(AppGatewayServiceTest, DisconnectDetachesInternalConnectionWithoutKick) {
  auto& chat = AttachChatBridge();
  Login(session_, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  // Client gone: the internal side closes quietly - an EOF on chat, no kick
  // to a session that is already going away.
  HandleDisconnect(session_, state_, nullptr, bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.EofCount() >= 1; }, std::chrono::seconds(5)));
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
  const auto frames = ReceivedPackets(*session_);
  for (const auto& f : frames) {
    EXPECT_NE(f.msg_id(), chirp::gateway::KICK_NOTIFY);
  }
}

// ---------------------------------------------------------------------------
// Batch 19: the auth-backed login/logout paths, the four never-dispatched
// RPC cases, the subscription relay's failure arms and the dispatch parse
// guards (handler-face gap closure on the same seam).
// ---------------------------------------------------------------------------

namespace {

// Loopback stand-in for the auth service, same protocol shape as the game
// gateway's fake: the AuthClient opens one TCP connection per job, sends one
// framed LOGIN_REQ/LOGOUT_REQ and reads one reply; this fake answers each
// request with the scripted response.
class FakeAuthServer {
 public:
  FakeAuthServer()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeAuthServer() {
    asio::post(io_, [this] {
      asio::error_code ec;
      acceptor_.close(ec);
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& s : sockets_) {
        s->close(ec);
      }
    });
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint16_t port() const { return port_; }

  void set_login_code(chirp::common::ErrorCode code) {
    std::lock_guard<std::mutex> lock(mu_);
    login_code_ = code;
  }
  void set_login_user_id(std::string user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    login_user_id_ = std::move(user_id);
  }
  void set_login_kick_reason(std::string reason) {
    std::lock_guard<std::mutex> lock(mu_);
    login_kick_reason_ = std::move(reason);
  }
  void set_logout_code(chirp::common::ErrorCode code) {
    std::lock_guard<std::mutex> lock(mu_);
    logout_code_ = code;
  }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mu_);
        sockets_.push_back(sock);
      }
      DoAccept();
      DoRead(sock, std::string());
    });
  }

  void DoRead(const std::shared_ptr<asio::ip::tcp::socket>& sock, std::string buf) {
    auto chunk = std::make_shared<std::array<char, 4096>>();
    sock->async_read_some(
        asio::buffer(*chunk),
        [this, sock, buf, chunk](const std::error_code& ec, std::size_t n) mutable {
          if (ec) {
            return;
          }
          buf.append(chunk->data(), n);
          for (;;) {
            if (buf.size() < 4) {
              break;
            }
            const uint32_t size =
                chirp::network::ReadU32BE(reinterpret_cast<const uint8_t*>(buf.data()));
            if (size > 64u * 1024u * 1024u || buf.size() < 4 + size) {
              break;
            }
            Packet pkt;
            if (pkt.ParseFromArray(buf.data() + 4, static_cast<int>(size))) {
              Reply(pkt, sock);
            }
            buf.erase(0, 4 + size);
          }
          DoRead(sock, std::move(buf));
        });
  }

  void Reply(const Packet& pkt, const std::shared_ptr<asio::ip::tcp::socket>& sock) {
    std::string out;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
        chirp::auth::LoginResponse resp;
        resp.set_code(login_code_);
        resp.set_user_id(login_user_id_);
        resp.set_session_id("auth-sess-1");
        resp.set_server_time(1);
        if (!login_kick_reason_.empty()) {
          resp.mutable_kick()->set_reason(login_kick_reason_);
        }
        out = chirp_test::FramePacket(
            chirp_test::MakeRawPacket(chirp::gateway::LOGIN_RESP, pkt.sequence(),
                                      resp.SerializeAsString()));
      } else if (pkt.msg_id() == chirp::gateway::LOGOUT_REQ) {
        chirp::auth::LogoutResponse resp;
        resp.set_code(logout_code_);
        resp.set_server_time(1);
        out = chirp_test::FramePacket(
            chirp_test::MakeRawPacket(chirp::gateway::LOGOUT_RESP, pkt.sequence(),
                                      resp.SerializeAsString()));
      } else {
        return;
      }
    }
    asio::error_code ec;
    asio::write(*sock, asio::buffer(out), ec);
  }

  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  std::thread thread_;
  std::mutex mu_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
  uint16_t port_{0};

  chirp::common::ErrorCode login_code_{chirp::common::OK};
  chirp::common::ErrorCode logout_code_{chirp::common::OK};
  std::string login_user_id_{"authed_alice"};
  std::string login_kick_reason_;
};

// Minimal hub stand-in for the relay's failure arm: completes the service
// auth handshake, then answers the subscription RPC with a well-formed
// Packet whose BODY cannot parse as the expected response - the edge must
// surface that as INTERNAL_ERROR instead of relaying garbage.
class GarbageReplyHub {
 public:
  GarbageReplyHub()
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~GarbageReplyHub() {
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
      DoRead(sock, std::string());
      DoAccept();
    });
  }

  void DoRead(const std::shared_ptr<asio::ip::tcp::socket>& sock, std::string buf) {
    auto chunk = std::make_shared<std::array<char, 4096>>();
    sock->async_read_some(
        asio::buffer(*chunk),
        [this, sock, buf, chunk](const std::error_code& ec, std::size_t n) mutable {
          if (ec) {
            return;
          }
          buf.append(chunk->data(), n);
          for (;;) {
            if (buf.size() < 4) {
              break;
            }
            const uint32_t size =
                chirp::network::ReadU32BE(reinterpret_cast<const uint8_t*>(buf.data()));
            if (size > 64u * 1024u * 1024u || buf.size() < 4 + size) {
              break;
            }
            Packet pkt;
            if (pkt.ParseFromArray(buf.data() + 4, static_cast<int>(size))) {
              Reply(pkt, sock);
            }
            buf.erase(0, 4 + size);
          }
          DoRead(sock, std::move(buf));
        });
  }

  void Reply(const Packet& pkt, const std::shared_ptr<asio::ip::tcp::socket>& sock) {
    chirp::gateway::Packet resp;
    resp.set_sequence(pkt.sequence());
    if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        auth_count_++;
      }
      chirp::game_server_gateway::ServerAuthResponse r;
      r.set_code(chirp::common::OK);
      r.set_heartbeat_interval_seconds(1);
      resp.set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
      resp.set_body(r.SerializeAsString());
    } else if (pkt.msg_id() == chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ) {
      resp.set_msg_id(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
      resp.set_body("\xff\xfe not a response");
    } else {
      return;  // heartbeats etc. go unanswered
    }
    auto framed = chirp::network::ProtobufFraming::Encode(resp);
    asio::error_code ec;
    asio::write(*sock, asio::buffer(framed), ec);
  }

  std::mutex mu_;
  size_t auth_count_{0};
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::thread thread_;
};

// True when the session holds a frame with the given msg_id and sequence.
bool HasFrame(const MockSession& s, chirp::gateway::MsgID id, int64_t seq) {
  for (const auto& framed : s.sent) {
    Packet p;
    if (DecodeFramed(framed, &p) && p.msg_id() == id && p.sequence() == seq) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_F(AppGatewayServiceTest, ArgHelpersHitMissDanglingAndGarbage) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "5400";
  char dangling[] = "--dangling";
  char* argv[] = {bin, port, value, dangling};
  EXPECT_EQ(GetArg(4, argv, "--port", "5200"), "5400");
  EXPECT_EQ(GetArg(4, argv, "--missing", "5200"), "5200");
  EXPECT_EQ(GetArg(4, argv, "--dangling", "5200"), "5200");
  EXPECT_EQ(ParseU16Arg(4, argv, "--port", 123), 5400);
  EXPECT_EQ(ParseU16Arg(4, argv, "--gone", 123), 123);
  char garbage[] = "not-a-number";
  char* argv2[] = {bin, port, garbage};
  EXPECT_EQ(ParseU16Arg(3, argv2, "--port", 123), 0);  // std::atoi -> 0
}

// The auth-backed login failure arm: a connect-refused auth service still
// invokes the callback with INTERNAL_ERROR, and the edge relays it verbatim
// without binding anything.
TEST_F(AppGatewayServiceTest, AuthBackedLoginFailureRelaysErrorCode) {
  asio::io_context io;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", 1);
  chirp::auth::LoginRequest req;
  req.set_token("tok");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGIN_REQ, 9, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*session_, chirp::gateway::LOGIN_RESP, 9); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// OK from auth but with an empty user id AND an empty token: the derived
// identity is empty and the login is refused before any binding.
TEST_F(AppGatewayServiceTest, AuthBackedLoginEmptyIdentityRejected) {
  asio::io_context io;
  FakeAuthServer auth_srv;
  auth_srv.set_login_user_id("");
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;  // empty token
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*session_, chirp::gateway::LOGIN_RESP, 1); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// OK from auth binds the relayed identity; a same-platform re-login kicks
// the previous session with the auth-provided kick reason.
TEST_F(AppGatewayServiceTest, AuthBackedLoginBindsAndKicksWithAuthReason) {
  asio::io_context io;
  FakeAuthServer auth_srv;
  auth_srv.set_login_kick_reason("auth said so");
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  req.set_platform("ios");
  auto s1 = std::make_shared<MockSession>();
  HandleClientPacket(s1,
                     MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*s1, chirp::gateway::LOGIN_RESP, 1); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse first;
  ASSERT_TRUE(LastBody(*s1, &first));
  ASSERT_EQ(first.code(), chirp::common::OK);
  EXPECT_EQ(first.user_id(), "authed_alice");  // relayed from the auth resp
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, s1).user_id, "authed_alice");

  auto s2 = std::make_shared<MockSession>();
  HandleClientPacket(s2,
                     MakePacket(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*s2, chirp::gateway::LOGIN_RESP, 2); },
                        std::chrono::seconds(5)));

  bool kicked = false;
  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      kicked = true;
      chirp::auth::KickNotify body;
      ASSERT_TRUE(body.ParseFromString(pkt.body()));
      EXPECT_EQ(body.reason(), "auth said so");  // auth resp beats platform text
    }
  }
  EXPECT_TRUE(kicked);
  EXPECT_TRUE(s1->close_after_send);
}

// Without a kick payload in the auth response the kick reason falls back to
// the platform text.
TEST_F(AppGatewayServiceTest, AuthBackedLoginKickFallsBackToPlatformReason) {
  asio::io_context io;
  FakeAuthServer auth_srv;  // no kick scripted
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  req.set_platform("ios");
  auto s1 = std::make_shared<MockSession>();
  HandleClientPacket(s1,
                     MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*s1, chirp::gateway::LOGIN_RESP, 1); },
                        std::chrono::seconds(5)));

  auto s2 = std::make_shared<MockSession>();
  HandleClientPacket(s2,
                     MakePacket(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*s2, chirp::gateway::LOGIN_RESP, 2); },
                        std::chrono::seconds(5)));

  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      chirp::auth::KickNotify body;
      ASSERT_TRUE(body.ParseFromString(pkt.body()));
      EXPECT_EQ(body.reason(), chirp::network::LoginKickReason("ios"));
    }
  }
}

// With a redis session manager configured, the LOGIN_RESP goes out only
// after the cross-instance claim callback fires (even a refused redis still
// delivers the callback with an empty previous owner).
TEST_F(AppGatewayServiceTest, AuthBackedLoginWithRedisManagerRespondsAfterClaim) {
  auto& chat = AttachChatBridge();
  FakeAuthServer auth_srv;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());
  auto redis = std::make_shared<chirp::gateway::RedisSessionManager>(
      io_, "127.0.0.1", 1, "inst-test", 60, nullptr);

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGIN_REQ, 3, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, redis, nullptr, nullptr, bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return HasFrame(*session_, chirp::gateway::LOGIN_RESP, 3); },
                        std::chrono::seconds(5)));
  // The chat pipe attaches inside the claim callback, after the
  // cross-instance claim settles.
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).user_id, "authed_alice");
}

// The last never-dispatched RPC case: the subscriptions listing rides the
// same pinned forwarding path and relays the hub's response verbatim.
TEST_F(AppGatewayServiceTest, GetPlayerSubscriptionsForwardedPinned) {
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
  ASSERT_TRUE(WaitForIo(io, [&] { return fake.CountAuth() >= 1; }, std::chrono::seconds(5)));
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::game_server_gateway::GetPlayerSubscriptionsRequest req;
  req.set_player_id("mallory");  // must be overwritten with the authenticated user
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ, 34,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);
  ASSERT_TRUE(WaitForIo(io,
                        [&] { return HasFrame(*session_, chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP, 34); },
                        std::chrono::seconds(5)));

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));

  bool found = false;
  for (const auto& body : fake.Received()) {
    chirp::game_server_gateway::GetPlayerSubscriptionsRequest candidate;
    if (candidate.ParseFromString(body) && candidate.player_id() == "alice") {
      found = true;
    }
  }
  EXPECT_TRUE(found);

  chirp::game_server_gateway::GetPlayerSubscriptionsResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  ASSERT_EQ(resp.subscriptions_size(), 1);
  EXPECT_EQ(resp.subscriptions(0).subscription_id(), "sub-fake-1");
}

// The auth-backed login must attach the chat pipe too (inline, no redis).
TEST_F(AppGatewayServiceTest, AuthBackedLoginAttachesBridgePipe) {
  auto& chat = AttachChatBridge();
  FakeAuthServer auth_srv;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGIN_REQ, 4, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return HasFrame(*session_, chirp::gateway::LOGIN_RESP, 4); },
                        std::chrono::seconds(5)));
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

// A failing auth service on logout relays the error and keeps the binding
// (only the auth-confirmed OK path releases and closes).
TEST_F(AppGatewayServiceTest, AuthBackedLogoutFailureKeepsBinding) {
  Login(session_, "alice");
  asio::io_context io;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", 1);

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGOUT_REQ, 6, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return HasFrame(*session_, chirp::gateway::LOGOUT_RESP, 6); },
                        std::chrono::seconds(5)));
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_FALSE(session_->close_after_send);
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).user_id, "alice");
}

TEST_F(AppGatewayServiceTest, AuthBackedLogoutOkCloses) {
  Login(session_, "alice");
  asio::io_context io;
  FakeAuthServer auth_srv;  // logout_code defaults to OK
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io, "127.0.0.1", auth_srv.port());

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::LOGOUT_REQ, 7, req.SerializeAsString()).SerializeAsString(),
                     state_, auth, nullptr, nullptr, nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io, [&] { return session_->close_after_send; }, std::chrono::seconds(5)));
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// The two device RPCs no other test dispatches, with the same pinning and
// relay contract as register/update (their fake replies are already wired).
TEST_F(AppGatewayServiceTest, UnregisterAndGetUserDevicesForwardedPinned) {
  FakeNotificationServer fake;
  asio::io_context io;
  auto notification = std::make_unique<chirp::app_notification::NotificationClient>(
      io, "127.0.0.1", fake.port());

  Login(session_, "alice");

  chirp::app_notification::UnregisterDeviceRequest unreg;
  unreg.set_device_id("dev-2");
  unreg.set_user_id("spoofed-user");  // must be overwritten by the gateway
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::UNREGISTER_DEVICE_REQ, 21,
                                unreg.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io,
                        [&] { return HasFrame(*session_, chirp::gateway::UNREGISTER_DEVICE_RESP, 21); },
                        std::chrono::seconds(5)));

  chirp::app_notification::GetUserDevicesRequest list;
  list.set_user_id("spoofed-user");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::GET_USER_DEVICES_REQ, 22,
                                list.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, notification.get(), nullptr, nullptr);
  ASSERT_TRUE(WaitForIo(io,
                        [&] { return HasFrame(*session_, chirp::gateway::GET_USER_DEVICES_RESP, 22); },
                        std::chrono::seconds(5)));
  DrainAndDropNotificationClientForTest(*notification);
  notification.reset();

  const auto received = fake.Received();
  ASSERT_EQ(received.size(), 2u);
  chirp::app_notification::UnregisterDeviceRequest fwd_unreg;
  ASSERT_TRUE(fwd_unreg.ParseFromString(received[0]));
  EXPECT_EQ(fwd_unreg.user_id(), "alice");
  EXPECT_EQ(fwd_unreg.device_id(), "dev-2");
  chirp::app_notification::GetUserDevicesRequest fwd_list;
  ASSERT_TRUE(fwd_list.ParseFromString(received[1]));
  EXPECT_EQ(fwd_list.user_id(), "alice");

  chirp::app_notification::UnregisterDeviceResponse unreg_resp;
  ASSERT_TRUE(LastBody(*session_, &unreg_resp));
  EXPECT_EQ(unreg_resp.code(), chirp::common::OK);
}

// The subscription relay's parse-fail arm: the hub's reply Packet is valid
// framing but its body does not parse as the expected response, so nothing
// is relayed and the client gets a bare INTERNAL_ERROR.
TEST_F(AppGatewayServiceTest, SubscriptionGarbageHubBodyReportsInternalError) {
  GarbageReplyHub hub;
  asio::io_context io;
  chirp::network::ServerGatewayPeer::Options opts;
  opts.host = "127.0.0.1";
  opts.port = hub.port();
  opts.service_id = "app_gateway";
  opts.secret = "edge-secret";
  opts.reconnect_delay_seconds = 1;
  auto sg = chirp::network::ServerGatewayPeer::Create(io, opts, nullptr, nullptr);
  sg->Start();

  Login(session_, "alice");
  // Handshake completes on the hub's thread while the peer strand only
  // advances on poll: wait for the auth exchange, then settle.
  ASSERT_TRUE(WaitForIo(io, [&] { return hub.CountAuth() >= 1; }, std::chrono::seconds(5)))
      << "service auth handshake did not complete";
  WaitForIo(io, [] { return false; }, std::chrono::milliseconds(100));

  chirp::game_server_gateway::SubscribePlayerChannelRequest req;
  req.set_game_id("game_a");
  req.set_channel_id("world");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 31,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);
  ASSERT_TRUE(WaitForIo(io,
                        [&] { return HasFrame(*session_, chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP, 31); },
                        std::chrono::seconds(5)));

  chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);

  sg->Stop();
  WaitForIo(io, [&] { return true; }, std::chrono::milliseconds(50));
}

// The subscription relay's transport-failure arm: an authenticated client
// with a peer that was never started fails fast with SERVER_UNAVAILABLE as
// a bare code (nothing was relayed).
TEST_F(AppGatewayServiceTest, SubscriptionUnstartedPeerFailsFastUnavailable) {
  asio::io_context io;
  chirp::network::ServerGatewayPeer::Options opts;
  opts.host = "127.0.0.1";
  opts.port = 1;
  opts.service_id = "app_gateway";
  opts.secret = "edge-secret";
  auto sg = chirp::network::ServerGatewayPeer::Create(io, opts, nullptr, nullptr);
  // Deliberately NOT started: SendRpc fails fast.

  Login(session_, "alice");
  chirp::game_server_gateway::UnsubscribePlayerChannelRequest req;
  req.set_subscription_id("sub-1");
  HandleClientPacket(session_,
                     MakePacket(chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ, 32,
                                req.SerializeAsString()).SerializeAsString(),
                     state_, nullptr, nullptr, nullptr, sg.get(), nullptr);
  ASSERT_TRUE(WaitForIo(io,
                        [&] { return HasFrame(*session_, chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP, 32); },
                        std::chrono::seconds(5)));
  chirp::game_server_gateway::UnsubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
}

// A garbage body on the subscription path answers INVALID_PARAM before any
// identity or plane check.
TEST_F(AppGatewayServiceTest, SubscriptionGarbageBodyRejected) {
  SendFrame(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, 33, "\xff\xfe nope");
  chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AppGatewayServiceTest, LoginGarbageBodyRejected) {
  SendFrame(chirp::gateway::LOGIN_REQ, 1, "\xff\xfe nope");
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AppGatewayServiceTest, LogoutGarbageBodyRejected) {
  SendFrame(chirp::gateway::LOGOUT_REQ, 1, "\xff\xfe nope");
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AppGatewayServiceTest, HeartbeatEchoesTimestampAndToleratesGarbageBody) {
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(4321);
  SendFrame(chirp::gateway::HEARTBEAT_PING, 5, ping.SerializeAsString());
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(LastBody(*session_, &pong));
  EXPECT_EQ(pong.timestamp(), 4321);
  EXPECT_GT(pong.server_time(), 0);

  const size_t sent_before = session_->sent.size();
  SendFrame(chirp::gateway::HEARTBEAT_PING, 6, std::string("\xff junk"));
  EXPECT_EQ(session_->sent.size(), sent_before);  // garbage ping: silent
}

// A payload that is not even a Packet is dropped by the dispatcher with only
// a warning - no reply, no close.
TEST_F(AppGatewayServiceTest, DispatchToleratesNonPacketPayload) {
  HandleClientPacket(session_, std::string("\xff\xfe\xfd not a Packet"),
                     state_, nullptr, nullptr, nullptr, nullptr, nullptr);
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_FALSE(session_->closed);
}

// The disconnect path releases the cross-instance claim when a redis
// manager is configured (fire-and-forget: a refused redis is fine, the
// binding must still be gone).
TEST_F(AppGatewayServiceTest, DisconnectWithRedisManagerReleasesClaim) {
  asio::io_context io;
  auto redis = std::make_shared<chirp::gateway::RedisSessionManager>(
      io, "127.0.0.1", 1, "inst-test", 60, nullptr);
  Login(session_, "alice");

  HandleDisconnect(session_, state_, redis, nullptr);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
  io.poll();
  io.restart();
}

}  // namespace
