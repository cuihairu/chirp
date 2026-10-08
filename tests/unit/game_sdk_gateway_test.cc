// Unit tests for the game gateway edge (services/game/sdk_gateway/src/main.cc).
// Same include-the-binary trick as app_gateway_service_test.cc: main() is
// renamed and the anonymous-namespace handlers are driven directly. Focus is
// the scaffold-login session binding (commit 2c08a21) and the 2xxx forwarding
// gate: without --auth_host, a token login must still bind the registry or
// every chat packet is silently dropped — the deployment shape the
// architecture doc calls first-class (game backend issues tokens, game_chat
// verifies locally, no auth service).
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "network/session_registry.h"
#include "fake_chat_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

// Relative path on purpose: a bare "main.cc" would resolve through the -I
// path to this very file's directory first and is ambiguous anyway.
#define main chirp_game_gateway_main
#include "../../services/game/sdk_gateway/src/main.cc"
#undef main

using Packet = chirp::gateway::Packet;

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

// Pumps the fixture io_context until pred holds (the bridge runs on it).
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

class GameSdkGatewayTest : public ::testing::Test {
 protected:
  std::shared_ptr<chirp::network::SessionRegistry> state_ =
      std::make_shared<chirp::network::SessionRegistry>();
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();
  // Lives on the fixture so chat-bridge tests can pump it through WaitForIo;
  // the bridge is created on demand via AttachServiceBridge().
  asio::io_context io_;
  std::unique_ptr<chirp::gateway::ServiceBridge> bridge_;
  std::unique_ptr<chirp_test::FakeChatServer> chat_;

  // Points the edge's chat pipeline at a loopback fake chat server (same
  // helper the app_sdk_gateway tests use). The default service id matches
  // the game gateway's --chat_service_id default.
  chirp_test::FakeChatServer& AttachServiceBridge() {
    chat_ = std::make_unique<chirp_test::FakeChatServer>(chirp::common::OK, chirp::common::OK);
    bridge_ = std::make_unique<chirp::gateway::ServiceBridge>(io_, "127.0.0.1", chat_->port(),
                                                           "gateway", "edge-secret");
    return *chat_;
  }

  // auth/redis default to null: the scaffold login path (no auth service
  // configured) — exactly the shape the binding fix targets. Batch-17 tests
  // pass real (loopback or refused-port) doubles for the auth-backed arms.
  void SendFrame(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
                 chirp::gateway::ServiceBridge* bridge = nullptr,
                 const std::shared_ptr<chirp::gateway::AuthClient>& auth = nullptr,
                 const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis = nullptr,
                 const std::shared_ptr<MockSession>& to = nullptr,
                 chirp::gateway::ServiceBridge* search_bridge = nullptr) {
    HandleClientPacket(state_, auth, redis, bridge, search_bridge, to ? to : session_,
                       MakePacket(id, seq, body));
  }

  // Scaffold-login `user` on `session` and return the assigned session_id.
  std::string Login(const std::shared_ptr<MockSession>& session, const std::string& user,
                    chirp::gateway::ServiceBridge* bridge = nullptr) {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    HandleClientPacket(state_, nullptr, nullptr, bridge, nullptr, session,
                       MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()));
    chirp::auth::LoginResponse resp;
    EXPECT_FALSE(session->sent.empty());
    if (!session->sent.empty()) {
      EXPECT_TRUE(LastBody(*session, &resp));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    return resp.session_id();
  }
};

// The 2c08a21 regression test: the scaffold branch must bind the session
// registry like the auth-backed path, or the 2xxx forwarding gate
// (GetAuthenticatedSession().user_id empty) silently drops every chat packet.
TEST_F(GameSdkGatewayTest, ScaffoldLoginBindsSessionRegistry) {
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

TEST_F(GameSdkGatewayTest, ScaffoldLoginEmptyTokenRejected) {
  chirp::auth::LoginRequest req;  // empty token
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // Nothing bound.
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(GameSdkGatewayTest, ReLoginKicksPreviousSession) {
  auto s1 = std::make_shared<MockSession>();
  Login(s1, "alice");

  auto s2 = std::make_shared<MockSession>();
  Login(s2, "alice");

  // s1 must have received the kick frame and been closed after send.
  bool kicked = false;
  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      kicked = true;
      chirp::auth::KickNotify body;
      ASSERT_TRUE(body.ParseFromString(pkt.body()));
      EXPECT_EQ(body.reason(), "logged in on another device");
    }
  }
  EXPECT_TRUE(kicked);
  EXPECT_TRUE(s1->close_after_send);
}

// The TODO-18 end-to-end shape: no auth service configured, chat bridge
// attached. A scaffold login must open the 2xxx gate — before the fix the
// packet was silently dropped after a successful LOGIN_RESP.
TEST_F(GameSdkGatewayTest, ChatPacketAfterScaffoldLoginReachesChatPipe) {
  auto& chat = AttachServiceBridge();
  Login(session_, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  // Identity intact: msg_id, sequence and body survive the relay untouched.
  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 42, "body-bytes", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) > 0; },
                        std::chrono::seconds(5)));
  const auto sent = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  ASSERT_FALSE(sent.empty());
  EXPECT_EQ(sent.back().sequence(), 42);
  EXPECT_EQ(sent.back().body(), "body-bytes");
}

// Unauthenticated 2xxx stays dropped even while another session holds a live
// bridge pipe: the gate is the session binding, not the bridge's existence.
TEST_F(GameSdkGatewayTest, UnauthenticatedChatPacketNotForwarded) {
  auto& chat = AttachServiceBridge();
  auto authed = std::make_shared<MockSession>();
  Login(authed, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 7, "not-allowed", bridge_.get());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(chat.Count(chirp::gateway::SEND_MESSAGE_REQ), 0);
}

// ---------------------------------------------------------------------------
// Batch 17: the auth-backed login path, the whole logout flow and the
// dispatch edges (handler-face coverage follow-up on the same seam).
// ---------------------------------------------------------------------------

namespace {

// Loopback stand-in for the auth service. The AuthClient opens one TCP
// connection per job, sends one framed LOGIN_REQ/LOGOUT_REQ and reads one
// reply; this fake answers each request with the scripted response and keeps
// reading (jobs after the first get a fresh connection anyway).
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
        [this, sock, buf, chunk](const asio::error_code& ec, std::size_t n) mutable {
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

// Parses the most recent frame's outer Packet only (msg_id + body bytes).
bool LastFrame(const MockSession& s, Packet* out) {
  if (s.sent.empty()) {
    return false;
  }
  return DecodeFramed(s.sent.back(), out);
}

}  // namespace

TEST_F(GameSdkGatewayTest, ArgHelpersHitMissDanglingAndGarbage) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "5100";
  char dangling[] = "--dangling";
  char* argv[] = {bin, port, value, dangling};
  EXPECT_EQ(GetArg(4, argv, "--port", "5000"), "5100");
  EXPECT_EQ(GetArg(4, argv, "--missing", "5000"), "5000");
  EXPECT_EQ(GetArg(4, argv, "--dangling", "5000"), "5000");
  EXPECT_EQ(ParseU16Arg(4, argv, "--port", 123), 5100);
  EXPECT_EQ(ParseU16Arg(4, argv, "--gone", 123), 123);
  char garbage[] = "not-a-number";
  char* argv2[] = {bin, port, garbage};
  EXPECT_EQ(ParseU16Arg(3, argv2, "--port", 123), 0);  // std::atoi -> 0
}

TEST_F(GameSdkGatewayTest, KickSessionDefaultsReasonWhenEmpty) {
  KickSession(session_, "");
  Packet pkt;
  ASSERT_TRUE(LastFrame(*session_, &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::KICK_NOTIFY);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(pkt.body()));
  EXPECT_EQ(kick.reason(), "kicked");  // empty reason falls back
  EXPECT_TRUE(session_->close_after_send);
}

// The auth-backed login failure arm: a connect-refused auth service still
// invokes the callback with INTERNAL_ERROR, and the edge relays it verbatim
// without binding anything.
TEST_F(GameSdkGatewayTest, AuthBackedLoginFailureRelaysErrorCode) {
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", 1);
  chirp::auth::LoginRequest req;
  req.set_token("tok");
  SendFrame(chirp::gateway::LOGIN_REQ, 9, req.SerializeAsString(),
            /*bridge=*/nullptr, auth);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !session_->sent.empty(); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// OK from auth but with an empty user id AND an empty token: the derived
// identity is empty and the login is refused before any binding.
TEST_F(GameSdkGatewayTest, AuthBackedLoginEmptyIdentityRejected) {
  FakeAuthServer auth_srv;
  auth_srv.set_login_user_id("");
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;  // empty token
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(),
            /*bridge=*/nullptr, auth);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !session_->sent.empty(); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// OK from auth binds the relayed identity; a same-platform re-login kicks
// the previous session with the auth-provided kick reason.
TEST_F(GameSdkGatewayTest, AuthBackedLoginBindsAndKicksWithAuthReason) {
  FakeAuthServer auth_srv;
  auth_srv.set_login_kick_reason("auth said so");
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  auto s1 = std::make_shared<MockSession>();
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(),
            /*bridge=*/nullptr, auth, /*redis=*/nullptr, s1);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !s1->sent.empty(); }, std::chrono::seconds(5)));
  chirp::auth::LoginResponse first;
  ASSERT_TRUE(LastBody(*s1, &first));
  ASSERT_EQ(first.code(), chirp::common::OK);
  EXPECT_EQ(first.user_id(), "authed_alice");  // relayed from the auth resp
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, s1).user_id, "authed_alice");

  auto s2 = std::make_shared<MockSession>();
  SendFrame(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString(),
            /*bridge=*/nullptr, auth, /*redis=*/nullptr, s2);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !s2->sent.empty(); }, std::chrono::seconds(5)));

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
// the platform text (same wording the scaffold path uses).
TEST_F(GameSdkGatewayTest, AuthBackedLoginKickFallsBackToPlatformReason) {
  FakeAuthServer auth_srv;  // no kick scripted
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  auto s1 = std::make_shared<MockSession>();
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(),
            /*bridge=*/nullptr, auth, /*redis=*/nullptr, s1);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !s1->sent.empty(); }, std::chrono::seconds(5)));

  auto s2 = std::make_shared<MockSession>();
  SendFrame(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString(),
            /*bridge=*/nullptr, auth, /*redis=*/nullptr, s2);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !s2->sent.empty(); }, std::chrono::seconds(5)));

  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      chirp::auth::KickNotify body;
      ASSERT_TRUE(body.ParseFromString(pkt.body()));
      EXPECT_EQ(body.reason(), "logged in on another device");
    }
  }
}

// With a redis session manager configured, the LOGIN_RESP goes out only
// after the cross-instance claim callback fires (even a refused redis still
// delivers the callback with an empty previous owner).
TEST_F(GameSdkGatewayTest, AuthBackedLoginWithRedisManagerRespondsAfterClaim) {
  FakeAuthServer auth_srv;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());
  auto redis = std::make_shared<chirp::gateway::RedisSessionManager>(
      io_, "127.0.0.1", 1, "inst-test", 60, nullptr);

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  SendFrame(chirp::gateway::LOGIN_REQ, 3, req.SerializeAsString(),
            /*bridge=*/nullptr, auth, redis);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !session_->sent.empty(); },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).user_id, "authed_alice");
}

// The auth-backed login must attach the chat pipe too (not only the
// scaffold path): with no redis manager the attach happens inline.
TEST_F(GameSdkGatewayTest, AuthBackedLoginAttachesBridgePipe) {
  auto& chat = AttachServiceBridge();
  FakeAuthServer auth_srv;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  SendFrame(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), bridge_.get(), auth);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !session_->sent.empty(); },
                        std::chrono::seconds(5)));
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

// Same with a redis manager: the attach runs inside the claim callback,
// after the cross-instance claim settles.
TEST_F(GameSdkGatewayTest, AuthBackedLoginAttachesBridgePipeAfterClaim) {
  auto& chat = AttachServiceBridge();
  FakeAuthServer auth_srv;
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());
  auto redis = std::make_shared<chirp::gateway::RedisSessionManager>(
      io_, "127.0.0.1", 1, "inst-test", 60, nullptr);

  chirp::auth::LoginRequest req;
  req.set_token("tok");
  SendFrame(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString(), bridge_.get(), auth, redis);
  ASSERT_TRUE(WaitForIo(io_, [&] { return !session_->sent.empty(); },
                        std::chrono::seconds(5)));
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(GameSdkGatewayTest, LogoutRejectsEmptyUserUnauthAndSessionMismatch) {
  // Empty user id: rejected before anything else.
  chirp::auth::LogoutRequest empty;
  SendFrame(chirp::gateway::LOGOUT_REQ, 1, empty.SerializeAsString());
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // Not logged in: AUTH_FAILED even with a well-formed user id.
  chirp::auth::LogoutRequest foreign;
  foreign.set_user_id("alice");
  SendFrame(chirp::gateway::LOGOUT_REQ, 2, foreign.SerializeAsString());
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_FALSE(session_->close_after_send);

  // Logged in, but the request names someone else's session id.
  const std::string sid = Login(session_, "alice");
  chirp::auth::LogoutRequest stale;
  stale.set_user_id("alice");
  stale.set_session_id("someone-elses-session");
  SendFrame(chirp::gateway::LOGOUT_REQ, 3, stale.SerializeAsString());
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::SESSION_EXPIRED);
  EXPECT_FALSE(session_->close_after_send);
  // Still bound - nothing was released by the rejected attempts.
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).session_id, sid);
}

TEST_F(GameSdkGatewayTest, ScaffoldLogoutRemovesBindingAndCloses) {
  Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  SendFrame(chirp::gateway::LOGOUT_REQ, 4, req.SerializeAsString());

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(session_->close_after_send);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// Same flow with a redis manager configured: the claim release fires on the
// confirmed removal (refused redis is fine - the release is fire-and-forget).
TEST_F(GameSdkGatewayTest, ScaffoldLogoutWithRedisManagerReleasesClaim) {
  auto redis = std::make_shared<chirp::gateway::RedisSessionManager>(
      io_, "127.0.0.1", 1, "inst-test", 60, nullptr);
  Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  SendFrame(chirp::gateway::LOGOUT_REQ, 5, req.SerializeAsString(),
            /*bridge=*/nullptr, /*auth=*/nullptr, redis);

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(session_->close_after_send);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

// A failing auth service on logout relays the error and keeps the binding
// (only the auth-confirmed OK path releases and closes).
TEST_F(GameSdkGatewayTest, AuthBackedLogoutFailureKeepsBinding) {
  Login(session_, "alice");
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", 1);

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  const size_t sent_before = session_->sent.size();
  SendFrame(chirp::gateway::LOGOUT_REQ, 6, req.SerializeAsString(),
            /*bridge=*/nullptr, auth);
  ASSERT_TRUE(WaitForIo(io_, [&] { return session_->sent.size() > sent_before; },
                        std::chrono::seconds(5)));

  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_FALSE(session_->close_after_send);
  EXPECT_EQ(chirp::network::GetAuthenticatedSession(state_, session_).user_id, "alice");
}

TEST_F(GameSdkGatewayTest, AuthBackedLogoutOkCloses) {
  FakeAuthServer auth_srv;  // logout_code defaults to OK
  auto auth = std::make_shared<chirp::gateway::AuthClient>(io_, "127.0.0.1", auth_srv.port());
  Login(session_, "alice");

  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  SendFrame(chirp::gateway::LOGOUT_REQ, 7, req.SerializeAsString(),
            /*bridge=*/nullptr, auth);
  ASSERT_TRUE(WaitForIo(io_, [&] { return session_->close_after_send; },
                        std::chrono::seconds(5)));
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(chirp::network::GetAuthenticatedSession(state_, session_).user_id.empty());
}

TEST_F(GameSdkGatewayTest, LoginGarbageBodyRejected) {
  SendFrame(chirp::gateway::LOGIN_REQ, 1, "\xff\xfe not a message");
  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(GameSdkGatewayTest, LogoutGarbageBodyRejected) {
  SendFrame(chirp::gateway::LOGOUT_REQ, 1, "\xff\xfe not a message");
  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(GameSdkGatewayTest, HeartbeatEchoesTimestampAndToleratesGarbageBody) {
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

// Unknown ids stay silently ignored: an authed 2xxx with no bridge attached
// (chat not configured) and a plain unknown id both keep the connection.
TEST_F(GameSdkGatewayTest, UnknownAndUngatedChatIdsAreIgnored) {
  Login(session_, "alice");

  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 1, "no bridge configured");
  SendFrame(static_cast<chirp::gateway::MsgID>(9999), 2, "unknown");
  EXPECT_EQ(session_->sent.size(), 1u);  // just the LOGIN_RESP
  EXPECT_FALSE(session_->closed);
}
