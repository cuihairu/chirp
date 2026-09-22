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
  // the bridge is created on demand via AttachChatBridge().
  asio::io_context io_;
  std::unique_ptr<chirp::gateway::ChatBridge> bridge_;
  std::unique_ptr<chirp_test::FakeChatServer> chat_;

  // Points the edge's chat pipeline at a loopback fake chat server (same
  // helper the app_sdk_gateway tests use). The default service id matches
  // the game gateway's --chat_service_id default.
  chirp_test::FakeChatServer& AttachChatBridge() {
    chat_ = std::make_unique<chirp_test::FakeChatServer>(chirp::common::OK, chirp::common::OK);
    bridge_ = std::make_unique<chirp::gateway::ChatBridge>(io_, "127.0.0.1", chat_->port(),
                                                           "gateway", "edge-secret");
    return *chat_;
  }

  // auth=null, redis=null: the scaffold login path (no auth service
  // configured) — exactly the shape the binding fix targets.
  void SendFrame(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
                 chirp::gateway::ChatBridge* bridge = nullptr) {
    HandleClientPacket(state_, nullptr, nullptr, bridge, session_, MakePacket(id, seq, body));
  }

  // Scaffold-login `user` on `session` and return the assigned session_id.
  std::string Login(const std::shared_ptr<MockSession>& session, const std::string& user,
                    chirp::gateway::ChatBridge* bridge = nullptr) {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    HandleClientPacket(state_, nullptr, nullptr, bridge, session,
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
  EXPECT_EQ(resp.kick().reason(), "login from another device");

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
      EXPECT_EQ(body.reason(), "login from another device");
    }
  }
  EXPECT_TRUE(kicked);
  EXPECT_TRUE(s1->close_after_send);
}

// The TODO-18 end-to-end shape: no auth service configured, chat bridge
// attached. A scaffold login must open the 2xxx gate — before the fix the
// packet was silently dropped after a successful LOGIN_RESP.
TEST_F(GameSdkGatewayTest, ChatPacketAfterScaffoldLoginReachesChatPipe) {
  auto& chat = AttachChatBridge();
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
  auto& chat = AttachChatBridge();
  auto authed = std::make_shared<MockSession>();
  Login(authed, "alice", bridge_.get());
  ASSERT_TRUE(WaitForIo(io_, [&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                        std::chrono::seconds(5)));

  SendFrame(chirp::gateway::SEND_MESSAGE_REQ, 7, "not-allowed", bridge_.get());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(chat.Count(chirp::gateway::SEND_MESSAGE_REQ), 0);
}
