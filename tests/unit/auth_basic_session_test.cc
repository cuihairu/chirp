// Unit tests for the basic auth entry (services/app/auth/src/main.cc).
// main.cc is included with main() renamed; the hoisted HandleAuthPacket
// dispatch and the argv helpers are driven directly with an in-memory
// session mock — no listener, no signal loop.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

// Relative path on purpose: a bare "main.cc" would resolve through the -I
// path to a different service's entry file.
#define main chirp_auth_basic_main
#include "../../services/app/auth/src/main.cc"
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

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode.
bool DecodeFramed(const std::string& framed, Packet* out) {
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

Packet MakePacket(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

// Drives the hoisted frame handler with a serialized Packet payload.
void RunFrame(const std::shared_ptr<MockSession>& session,
              const Packet& pkt,
              const std::string& secret = "test-secret") {
  HandleAuthPacket(secret, session, pkt.SerializeAsString());
}

TEST(BasicAuthSessionTest, ArgHelpersParsePortGetArgAndRandomHex) {
  // ParsePort: default, --port, -p, dangling flag, garbage value.
  {
    char bin[] = "bin";
    char* argv[] = {bin};
    EXPECT_EQ(ParsePort(1, argv), 6000);
  }
  {
    char bin[] = "bin";
    char flag[] = "--port";
    char value[] = "7001";
    char* argv[] = {bin, flag, value};
    EXPECT_EQ(ParsePort(3, argv), 7001);
  }
  {
    char bin[] = "bin";
    char flag[] = "-p";
    char value[] = "7002";
    char* argv[] = {bin, flag, value};
    EXPECT_EQ(ParsePort(3, argv), 7002);
  }
  {
    char bin[] = "bin";
    char flag[] = "--port";  // dangling: no value after the flag
    char* argv[] = {bin, flag};
    EXPECT_EQ(ParsePort(2, argv), 6000);
  }
  {
    char bin[] = "bin";
    char flag[] = "--port";
    char garbage[] = "not-a-number";
    char* argv[] = {bin, flag, garbage};
    EXPECT_EQ(ParsePort(3, argv), 0);  // std::atoi -> 0
  }

  // GetArg: hit / miss / dangling flag falls back to the default.
  {
    char bin[] = "bin";
    char flag[] = "--jwt_secret";
    char value[] = "s3cret";
    char dangling[] = "--dangling";
    char* argv[] = {bin, flag, value, dangling};
    EXPECT_EQ(GetArg(4, argv, "--jwt_secret", "dev_secret"), "s3cret");
    EXPECT_EQ(GetArg(4, argv, "--missing", "dev_secret"), "dev_secret");
    EXPECT_EQ(GetArg(4, argv, "--dangling", "dev_secret"), "dev_secret");
  }

  // RandomHex: 2 hex chars per byte, lowercase alphabet, empty for 0.
  const std::string hex = RandomHex(16);
  ASSERT_EQ(hex.size(), 32u);
  for (const char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;
  }
  EXPECT_TRUE(RandomHex(0).empty());
}

TEST(BasicAuthSessionTest, LooksLikeJwtClassifiesTokens) {
  EXPECT_FALSE(LooksLikeJwt(""));
  EXPECT_FALSE(LooksLikeJwt("plain-token"));
  EXPECT_FALSE(LooksLikeJwt("only.one"));     // single dot
  EXPECT_TRUE(LooksLikeJwt("a.b.c"));         // two dots
  EXPECT_TRUE(LooksLikeJwt("h.p.payload.x")); // extra dots still qualify
}

TEST(BasicAuthSessionTest, NowMsWithinSaneWindow) {
  const int64_t before = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const int64_t now = NowMs();
  const int64_t after = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  EXPECT_LE(before, now);
  EXPECT_LE(now, after);
}

TEST(BasicAuthSessionTest, SendPacketFramesRoundtrip) {
  auto session = std::make_shared<MockSession>();
  SendPacket(session, chirp::gateway::LOGIN_RESP, 42, "payload-bytes");

  ASSERT_EQ(session->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent.back(), &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(pkt.sequence(), 42);
  EXPECT_EQ(pkt.body(), "payload-bytes");
}

TEST(BasicAuthSessionTest, DispatchNonPacketPayloadIsSilent) {
  auto session = std::make_shared<MockSession>();
  HandleAuthPacket("test-secret", session, std::string("\xff\xfe\xfd not a Packet"));
  EXPECT_TRUE(session->sent.empty());
  EXPECT_FALSE(session->closed);
}

TEST(BasicAuthSessionTest, DispatchUnknownMsgIdIsSilent) {
  auto session = std::make_shared<MockSession>();
  RunFrame(session, MakePacket(chirp::gateway::MsgID(9999), 7, "whatever"));
  EXPECT_TRUE(session->sent.empty());
  EXPECT_FALSE(session->closed);
}

TEST(BasicAuthSessionTest, LoginGarbageBodyRejected) {
  auto session = std::make_shared<MockSession>();
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 9, "\xff\xfe nope"));

  ASSERT_EQ(session->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent.back(), &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(pkt.sequence(), 9);  // sequence echoed
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_GT(resp.server_time(), 0);
}

TEST(BasicAuthSessionTest, LoginScaffoldTokenBindsAsUserId) {
  auto session = std::make_shared<MockSession>();
  chirp::auth::LoginRequest req;
  req.set_token("alice");  // no dots: scaffolding fallback
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString()));

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "alice");
  ASSERT_EQ(resp.session_id().size(), 32u);  // RandomHex(16)
  for (const char c : resp.session_id()) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;
  }
  EXPECT_TRUE(resp.kick_previous());
  EXPECT_EQ(resp.kick().reason(), "login from another device");
  EXPECT_GT(resp.server_time(), 0);
}

TEST(BasicAuthSessionTest, LoginEmptyTokenRejected) {
  auto session = std::make_shared<MockSession>();
  chirp::auth::LoginRequest req;  // empty token -> empty user_id
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString()));

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_TRUE(resp.user_id().empty());
  EXPECT_TRUE(resp.session_id().empty());
}

TEST(BasicAuthSessionTest, LoginValidJwtUsesSubject) {
  auto session = std::make_shared<MockSession>();
  const int64_t now = NowMs();
  const std::string token = chirp::common::JwtSignHS256("user_1", now, "test-secret", now + 3600);
  chirp::auth::LoginRequest req;
  req.set_token(token);
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 3, req.SerializeAsString()));

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_1");
  EXPECT_FALSE(resp.session_id().empty());
}

TEST(BasicAuthSessionTest, LoginJwtWrongSecretRejected) {
  auto session = std::make_shared<MockSession>();
  const int64_t now = NowMs();
  const std::string token =
      chirp::common::JwtSignHS256("user_1", now, "some_other_secret", now + 3600);
  chirp::auth::LoginRequest req;
  req.set_token(token);
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 4, req.SerializeAsString()));

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(resp.user_id().empty());
}

TEST(BasicAuthSessionTest, LoginMalformedJwtRejected) {
  auto session = std::make_shared<MockSession>();
  chirp::auth::LoginRequest req;
  req.set_token("a.b.c");  // two dots: LooksLikeJwt true, verify fails
  RunFrame(session, MakePacket(chirp::gateway::LOGIN_REQ, 5, req.SerializeAsString()));

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST(BasicAuthSessionTest, LogoutGarbageBodyRejected) {
  auto session = std::make_shared<MockSession>();
  RunFrame(session, MakePacket(chirp::gateway::LOGOUT_REQ, 6, "\xff\xfe nope"));

  ASSERT_EQ(session->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session->sent.back(), &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::LOGOUT_RESP);
  EXPECT_EQ(pkt.sequence(), 6);
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_GT(resp.server_time(), 0);
}

TEST(BasicAuthSessionTest, LogoutMissingFieldsRejectedAndCompleteAccepted) {
  // Both sides of the `user_id.empty() || session_id.empty()` disjunction,
  // plus the complete-request arm.
  const auto logout = [](const std::string& user_id, const std::string& session_id) {
    auto session = std::make_shared<MockSession>();
    chirp::auth::LogoutRequest req;
    req.set_user_id(user_id);
    req.set_session_id(session_id);
    RunFrame(session, MakePacket(chirp::gateway::LOGOUT_REQ, 8, req.SerializeAsString()));
    chirp::auth::LogoutResponse resp;
    EXPECT_TRUE(LastBody(*session, &resp)) << "no response frame";
    return resp;
  };

  chirp::auth::LogoutResponse missing_session = logout("alice", "");
  EXPECT_EQ(missing_session.code(), chirp::common::INVALID_PARAM);

  chirp::auth::LogoutResponse missing_user = logout("", "sess-1");
  EXPECT_EQ(missing_user.code(), chirp::common::INVALID_PARAM);

  chirp::auth::LogoutResponse complete = logout("alice", "sess-1");
  EXPECT_EQ(complete.code(), chirp::common::OK);
  EXPECT_GT(complete.server_time(), 0);
}

}  // namespace
