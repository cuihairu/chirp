// Enhanced auth entrypoint (chirp_app_auth) packet dispatch: the handler
// surface (argv helpers, framing, HandleAuthPacket's 9-case dispatch) lives
// in an anonymous namespace; pull the file in with main() renamed so the
// tests below drive the production dispatch directly - the same seam the
// chat/social/voice main tests use. Coverage batch 11: this file sits outside
// the gate's INCLUDE_PREFIXES universe (is_excluded drops main*), so the
// numbers come from the measurer over gcov JSON, not from the gate.
//
// The fixture mirrors auth_service_test.cc's AuthServiceTest setup (fake
// MySQL + fake sodium + InMemoryRedis behind FakeRedisServer), so no real
// MySQL/Redis is touched and the suite stays CI-safe.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio.hpp>

#include "auth_service.h"
#include "fake_mysql.h"
#include "fake_servers.h"
#include "in_memory_redis.h"
#include "password_hasher.h"
#include "token_generator.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp_test {
// Provided by fake_sodium.cc (linked into this test target).
namespace fake_sodium {
void Reset();
}  // namespace fake_sodium
}  // namespace chirp_test

// The enhanced main keeps its internals (HandleAuthPacket and the argv
// helpers) in an anonymous namespace; include the file with main() renamed
// so the tests can drive them directly.
#define main chirp_auth_enhanced_main
#include "../../services/app/auth/src/main_enhanced.cc"
#undef main

namespace {

using chirp::auth::AuthService;
using chirp::auth::PasswordHasher;
using chirp::auth::TokenGenerator;
namespace fake_mysql = chirp_test::fake_mysql;
namespace fake_sodium = chirp_test::fake_sodium;

// Pure in-memory Session mock: records every frame sent through it.
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  bool PeerHalfClosed() override { return half_closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
  bool half_closed = false;
};

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload.
bool DecodeFramed(const std::string& framed, chirp::gateway::Packet* out) {
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

std::vector<chirp::gateway::Packet> FramesOf(const MockSession& session,
                                             chirp::gateway::MsgID msg_id) {
  std::vector<chirp::gateway::Packet> matches;
  for (const auto& framed : session.sent) {
    chirp::gateway::Packet pkt;
    if (DecodeFramed(framed, &pkt) && pkt.msg_id() == msg_id) {
      matches.push_back(std::move(pkt));
    }
  }
  return matches;
}

// --- argv 辅助函数（纯函数面） ----------------------------------------------

TEST(AuthEnhancedMainHelpers, ArgvParsingAndClientIp) {
  // ParsePort：默认 6000 / --port / -p / 悬空旗标（无值跟随）。
  {
    char arg0[] = "prog";
    char* argv[] = {arg0, nullptr};
    EXPECT_EQ(ParsePort(1, argv), 6000);
  }
  {
    char arg0[] = "prog";
    char flag[] = "--port";
    char val[] = "7777";
    char* argv[] = {arg0, flag, val, nullptr};
    EXPECT_EQ(ParsePort(3, argv), 7777);
  }
  {
    char arg0[] = "prog";
    char flag[] = "-p";
    char val[] = "8081";
    char* argv[] = {arg0, flag, val, nullptr};
    EXPECT_EQ(ParsePort(3, argv), 8081);
  }
  {
    char arg0[] = "prog";
    char flag[] = "--port";
    char* argv[] = {arg0, flag, nullptr};
    EXPECT_EQ(ParsePort(2, argv), 6000);  // 后面没有值：不吃 argv 越界
  }

  // GetArg：命中 / 未命中回落默认 / 悬空键回落默认。
  {
    char arg0[] = "prog";
    char key[] = "--mode";
    char val[] = "x";
    char* argv[] = {arg0, key, val, nullptr};
    EXPECT_EQ(GetArg(3, argv, "--mode", "fallback"), "x");
    EXPECT_EQ(GetArg(3, argv, "--absent", "fallback"), "fallback");
  }
  {
    char arg0[] = "prog";
    char key[] = "--mode";
    char* argv[] = {arg0, key, nullptr};
    EXPECT_EQ(GetArg(2, argv, "--mode", "fallback"), "fallback");
  }

  // ParseIntArg：命中 / 未命中回落默认。
  {
    char arg0[] = "prog";
    char key[] = "--n";
    char val[] = "7";
    char* argv[] = {arg0, key, val, nullptr};
    EXPECT_EQ(ParseIntArg(3, argv, "--n", 0), 7);
    EXPECT_EQ(ParseIntArg(3, argv, "--absent", 42), 42);
  }

  // GetClientIp：尚未接出对端元数据，恒空串。
  EXPECT_EQ(GetClientIp(nullptr), "");
}

// --- 分发面 ------------------------------------------------------------------

class AuthEnhancedMainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
    fake_mysql::Reset();
    fake_sodium::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    // RedisAuthStore::Connect() probes with GET ping.
    redis_->SetDirect("ping", "pong");
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    AuthService::Config cfg;
    cfg.jwt_secret = jwt_secret_;
    cfg.redis_config.port = fake_->port();
    cfg.rate_limiter_config.max_login_attempts_per_minute = 100;
    cfg.rate_limiter_config.max_login_attempts_per_hour = 100;
    cfg.rate_limiter_config.max_registration_attempts_per_ip_per_hour = 100;
    service_ = std::make_shared<AuthService>(io_, cfg);
    ScriptSuccessfulInitialize();
    ASSERT_TRUE(service_->Initialize());
  }

  void TearDown() override {
    service_.reset();
    fake_.reset();
  }

  // Scripts the MySQL reply for a successful Initialize() (users table check).
  void ScriptSuccessfulInitialize() { fake_mysql::PushRows({{"users"}}); }

  // Scripts a successful password login for `username` (same row shape as
  // AuthServiceTest::ScriptSuccessfulLogin): NormalizeIdentifier (rate check),
  // VerifyCredentials, NormalizeIdentifier (RecordSuccess), then the active
  // session count below the max.
  void ScriptSuccessfulLogin(const std::string& username, const std::string& password) {
    const std::string hash = PasswordHasher::HashPassword(password);
    const std::vector<std::optional<std::string>> row = {
        "5", "user_1", username, "a@b.c", hash, "1", "2", "3", "1"};
    fake_mysql::PushRows({row});
    fake_mysql::PushRows({row});
    fake_mysql::PushRows({row});
    fake_mysql::PushRows({{"1"}});
  }

  // Runs one framed packet through the production dispatch.
  void Dispatch(chirp::gateway::MsgID msg_id, const std::string& body,
                const std::shared_ptr<MockSession>& session, int64_t seq = 1,
                bool scaffold = false) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_sequence(seq);
    pkt.set_body(body);
    HandleAuthPacket(service_, session, pkt.SerializeAsString(), scaffold);
  }

  template <typename Req>
  void DispatchReq(chirp::gateway::MsgID msg_id, const Req& req,
                   const std::shared_ptr<MockSession>& session, int64_t seq = 1,
                   bool scaffold = false) {
    Dispatch(msg_id, req.SerializeAsString(), session, seq, scaffold);
  }

  // Parses the latest response frame of `msg_id`; absent frames yield a
  // SERVER_UNAVAILABLE sentinel so a missing reply never reads as OK.
  template <typename Resp>
  Resp LastResponse(const MockSession& session, chirp::gateway::MsgID msg_id) {
    Resp resp;
    const auto frames = FramesOf(session, msg_id);
    if (!frames.empty()) {
      resp.ParseFromString(frames.back().body());
    } else {
      resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    }
    return resp;
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<AuthService> service_;
  // Explicit secret so the JWT arm below does not depend on the Config
  // default constant.
  std::string jwt_secret_ = "test_jwt_secret";
};

// 非 Packet 字节：静默丢弃（Warn 臂），不崩不回。
TEST_F(AuthEnhancedMainTest, GarbagePacketIsDroppedSilently) {
  auto session = std::make_shared<MockSession>();
  HandleAuthPacket(service_, session, std::string("\xde\xad\xbe\xef", 4), false);
  EXPECT_TRUE(session->sent.empty());
}

TEST_F(AuthEnhancedMainTest, RegisterMapsResultAndRejectsGarbageAndDuplicates) {
  // 垃圾 body：INVALID_PARAM + "Invalid request"。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::REGISTER_REQ, std::string("\xde\xad\xbe\xef", 4), garbage, 3);
  auto bad = LastResponse<chirp::auth::RegisterResponse>(
      *garbage, chirp::gateway::REGISTER_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(bad.error_message(), "Invalid request");
  EXPECT_EQ(FramesOf(*garbage, chirp::gateway::REGISTER_RESP)[0].sequence(), 3);

  // Happy：username/email 都空闲 → OK + user_id。
  fake_mysql::PushRows({{"0"}});  // username free
  fake_mysql::PushRows({{"0"}});  // email free
  chirp::auth::RegisterRequest req;
  req.set_username("alice");
  req.set_email("a@b.c");
  req.set_password("Str0ng!pass");
  req.set_display_name("Alice");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REGISTER_REQ, req, happy, 5);
  auto ok = LastResponse<chirp::auth::RegisterResponse>(
      *happy, chirp::gateway::REGISTER_RESP);
  EXPECT_EQ(ok.code(), chirp::common::OK) << ok.error_message();
  EXPECT_FALSE(ok.user_id().empty());
  EXPECT_GT(ok.server_time(), 0);

  // 用户名已占用：查询返回计数 1 → INVALID_PARAM + 专文案。
  fake_mysql::PushRows({{"1"}});
  fake_mysql::PushRows({{"0"}});
  auto dup = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REGISTER_REQ, req, dup);
  auto taken = LastResponse<chirp::auth::RegisterResponse>(
      *dup, chirp::gateway::REGISTER_RESP);
  EXPECT_EQ(taken.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(taken.error_message(), "Username already exists");

  // 弱口令：进 DB 之前就被拒（INVALID_PARAM）。
  chirp::auth::RegisterRequest weak;
  weak.set_username("bob");
  weak.set_password("short");
  auto frail = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REGISTER_REQ, weak, frail);
  auto rejected = LastResponse<chirp::auth::RegisterResponse>(
      *frail, chirp::gateway::REGISTER_RESP);
  EXPECT_EQ(rejected.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(rejected.error_message().empty());
}

TEST_F(AuthEnhancedMainTest, PasswordLoginMapsAllFieldsAndRejectsBad) {
  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::PASSWORD_LOGIN_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::PasswordLoginResponse>(
      *garbage, chirp::gateway::PASSWORD_LOGIN_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(bad.error_message(), "Invalid request");

  // 错口令：VerifyCredentials 失败 → AUTH_FAILED（三段 NormalizeIdentifier
  // 查询都回同一行，行内哈希对应正确口令——失败源于口令而非行内容）。
  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  const std::vector<std::optional<std::string>> row = {
      "5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"};
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  chirp::auth::PasswordLoginRequest wrong;
  wrong.set_identifier("alice");
  wrong.set_password("Wrong!pass1");
  wrong.set_device_id("d");
  wrong.set_platform("pc");
  auto denied_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::PASSWORD_LOGIN_REQ, wrong, denied_session);
  auto denied = LastResponse<chirp::auth::PasswordLoginResponse>(
      *denied_session, chirp::gateway::PASSWORD_LOGIN_RESP);
  EXPECT_EQ(denied.code(), chirp::common::AUTH_FAILED);

  // Happy：全字段映射（user/会话/双 token/到期/kick_previous）。
  ScriptSuccessfulLogin("alice", "Str0ng!pass");
  chirp::auth::PasswordLoginRequest req;
  req.set_identifier("alice");
  req.set_password("Str0ng!pass");
  req.set_device_id("phone");
  req.set_platform("ios");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::PASSWORD_LOGIN_REQ, req, happy, 9);
  auto ok = LastResponse<chirp::auth::PasswordLoginResponse>(
      *happy, chirp::gateway::PASSWORD_LOGIN_RESP);
  ASSERT_EQ(ok.code(), chirp::common::OK) << ok.error_message();
  EXPECT_EQ(ok.user_id(), "user_1");
  EXPECT_EQ(ok.username(), "alice");
  EXPECT_FALSE(ok.session_id().empty());
  EXPECT_FALSE(ok.access_token().empty());
  EXPECT_FALSE(ok.refresh_token().empty());
  EXPECT_GT(ok.access_token_expires_at(), 0);
  EXPECT_GT(ok.refresh_token_expires_at(), 0);
  EXPECT_GT(ok.server_time(), 0);
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::PASSWORD_LOGIN_RESP)[0].sequence(), 9);
}

TEST_F(AuthEnhancedMainTest, RefreshTokenArms) {
  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::REFRESH_TOKEN_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::RefreshTokenResponse>(
      *garbage, chirp::gateway::REFRESH_TOKEN_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(bad.error_message(), "Invalid request");

  // 空 refresh_token：RefreshAccessToken 拒空 → INVALID_PARAM。
  chirp::auth::RefreshTokenRequest empty_req;
  auto empty_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REFRESH_TOKEN_REQ, empty_req, empty_session);
  auto empty = LastResponse<chirp::auth::RefreshTokenResponse>(
      *empty_session, chirp::gateway::REFRESH_TOKEN_RESP);
  EXPECT_EQ(empty.code(), chirp::common::INVALID_PARAM);

  // 未知 token：查无此哈希 → AUTH_FAILED。
  chirp::auth::RefreshTokenRequest bogus;
  bogus.set_refresh_token("rt_bogus");
  auto bogus_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REFRESH_TOKEN_REQ, bogus, bogus_session);
  auto unknown = LastResponse<chirp::auth::RefreshTokenResponse>(
      *bogus_session, chirp::gateway::REFRESH_TOKEN_RESP);
  EXPECT_EQ(unknown.code(), chirp::common::AUTH_FAILED);

  // Happy：先经 service 真登录拿 refresh_token，再按 VerifyRefreshToken +
  // GetSession 的行形状脚本化（与 AuthServiceTest::RefreshTokenFlow 同款）。
  ScriptSuccessfulLogin("alice", "Str0ng!pass");
  const auto login = service_->Login("alice", "Str0ng!pass", "d", "pc", "");
  ASSERT_TRUE(login.success) << login.error_message;

  const std::vector<std::optional<std::string>> token_row = {
      "1", "tok_1", "user_1", login.session_id, "d", "placeholder", "1",
      "99999999999999", "0", "0"};
  const std::vector<std::optional<std::string>> session_row = {
      "1", login.session_id, "user_1", "d", "pc", "1", "99999999999999", "1", "1"};
  fake_mysql::PushRows({token_row});
  fake_mysql::PushRows({session_row});
  chirp::auth::RefreshTokenRequest req;
  req.set_refresh_token(login.refresh_token);
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REFRESH_TOKEN_REQ, req, happy, 4);
  auto ok = LastResponse<chirp::auth::RefreshTokenResponse>(
      *happy, chirp::gateway::REFRESH_TOKEN_RESP);
  ASSERT_EQ(ok.code(), chirp::common::OK) << ok.error_message();
  EXPECT_FALSE(ok.access_token().empty());
  EXPECT_GT(ok.access_token_expires_at(), 0);
  EXPECT_GT(ok.server_time(), 0);
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::REFRESH_TOKEN_RESP)[0].sequence(), 4);
}

// LOGIN_REQ 的旧版登录四臂：JWT / 会话 id / 双拒 / 脚手架回落。
TEST_F(AuthEnhancedMainTest, LegacyLoginTokenFourArms) {
  // 臂 1：有效 access token（fixture 秘钥签的真 JWT）→ OK + sub + 派生会话。
  const std::string jwt = TokenGenerator::GenerateAccessToken("user_42", jwt_secret_, 60);
  chirp::auth::LoginRequest req;
  req.set_token(jwt);
  auto jwt_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGIN_REQ, req, jwt_session, 2);
  auto ok = LastResponse<chirp::auth::LoginResponse>(*jwt_session,
                                                     chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(ok.code(), chirp::common::OK);
  EXPECT_EQ(ok.user_id(), "user_42");
  EXPECT_EQ(ok.session_id(), "user_42_sess");
  EXPECT_TRUE(ok.kick_previous());
  EXPECT_EQ(ok.kick().reason(), "session validated");
  EXPECT_EQ(FramesOf(*jwt_session, chirp::gateway::LOGIN_RESP)[0].sequence(), 2);

  // 臂 2：有效会话 id（经 service 真登录，Redis 在册）→ OK + 会话属主。
  ScriptSuccessfulLogin("alice", "Str0ng!pass");
  const auto login = service_->Login("alice", "Str0ng!pass", "d", "pc", "");
  ASSERT_TRUE(login.success);
  req.set_token(login.session_id);
  auto sess_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGIN_REQ, req, sess_session);
  auto sess_ok = LastResponse<chirp::auth::LoginResponse>(
      *sess_session, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(sess_ok.code(), chirp::common::OK);
  EXPECT_EQ(sess_ok.user_id(), "user_1");
  EXPECT_EQ(sess_ok.session_id(), "user_1_sess");

  // 臂 3：既非 JWT 也非会话、脚手架关 → AUTH_FAILED（不把任意 token 当身份）。
  req.set_token("not-a-jwt-not-a-session");
  auto denied_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGIN_REQ, req, denied_session);
  auto denied = LastResponse<chirp::auth::LoginResponse>(
      *denied_session, chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(denied.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(denied.user_id().empty());

  // 臂 4：同 token、脚手架开 → OK，user_id 原样取 token。
  auto scaffold_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGIN_REQ, req, scaffold_session, 1, /*scaffold=*/true);
  auto scaffold = LastResponse<chirp::auth::LoginResponse>(
      *scaffold_session, chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(scaffold.code(), chirp::common::OK);
  EXPECT_EQ(scaffold.user_id(), "not-a-jwt-not-a-session");
  EXPECT_EQ(scaffold.session_id(), "not-a-jwt-not-a-session_sess");

  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::LOGIN_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::LoginResponse>(*garbage,
                                                      chirp::gateway::LOGIN_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
}

TEST_F(AuthEnhancedMainTest, LogoutArms) {
  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::LOGOUT_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::LogoutResponse>(*garbage,
                                                       chirp::gateway::LOGOUT_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);

  // Happy：db（或 redis）成功 → OK。
  chirp::auth::LogoutRequest req;
  req.set_user_id("user_1");
  req.set_session_id("sess_1");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGOUT_REQ, req, happy, 6);
  auto ok = LastResponse<chirp::auth::LogoutResponse>(*happy,
                                                      chirp::gateway::LOGOUT_RESP);
  EXPECT_EQ(ok.code(), chirp::common::OK);
  EXPECT_GT(ok.server_time(), 0);
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::LOGOUT_RESP)[0].sequence(), 6);

  // INTERNAL_ERROR：db 撤销失败且 redis 已断开（db_result || redis_result
  // 双假）——Shutdown 放最后，它断掉服务自己的 redis 连接。
  fake_mysql::PushQueryError("revoke failed");
  service_->Shutdown();
  auto failed = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGOUT_REQ, req, failed);
  auto internal = LastResponse<chirp::auth::LogoutResponse>(
      *failed, chirp::gateway::LOGOUT_RESP);
  EXPECT_EQ(internal.code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthEnhancedMainTest, GetSessionsMapsRowsAndRejectsGarbage) {
  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::GET_SESSIONS_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::GetSessionsResponse>(
      *garbage, chirp::gateway::GET_SESSIONS_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);

  // Happy：行 → SessionInfo 映射（恒回 OK code，空列表也 OK）。
  fake_mysql::PushRows(
      {{"7", "sess_1", "user_1", "dev1", "web", "100", "200", "300", "1"}});
  chirp::auth::GetSessionsRequest req;
  req.set_user_id("user_1");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::GET_SESSIONS_REQ, req, happy, 8);
  auto ok = LastResponse<chirp::auth::GetSessionsResponse>(
      *happy, chirp::gateway::GET_SESSIONS_RESP);
  ASSERT_EQ(ok.code(), chirp::common::OK);
  ASSERT_EQ(ok.sessions_size(), 1);
  EXPECT_EQ(ok.sessions(0).session_id(), "sess_1");
  EXPECT_EQ(ok.sessions(0).device_id(), "dev1");
  EXPECT_EQ(ok.sessions(0).platform(), "web");
  EXPECT_EQ(ok.sessions(0).created_at(), 100);
  EXPECT_EQ(ok.sessions(0).last_activity_at(), 300);
  EXPECT_GT(ok.server_time(), 0);
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::GET_SESSIONS_RESP)[0].sequence(), 8);
}

TEST_F(AuthEnhancedMainTest, RevokeSessionArms) {
  // 垃圾 body：INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::REVOKE_SESSION_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::RevokeSessionResponse>(
      *garbage, chirp::gateway::REVOKE_SESSION_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);

  // Happy：撤销成功 → OK。
  chirp::auth::RevokeSessionRequest req;
  req.set_user_id("user_1");
  req.set_session_id("sess_2");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::REVOKE_SESSION_REQ, req, happy, 3);
  auto ok = LastResponse<chirp::auth::RevokeSessionResponse>(
      *happy, chirp::gateway::REVOKE_SESSION_RESP);
  EXPECT_EQ(ok.code(), chirp::common::OK);
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::REVOKE_SESSION_RESP)[0].sequence(), 3);
}

TEST_F(AuthEnhancedMainTest, ChangePasswordArms) {
  // 垃圾 body：INVALID_PARAM + "Invalid request"。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::CHANGE_PASSWORD_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  auto bad = LastResponse<chirp::auth::ChangePasswordResponse>(
      *garbage, chirp::gateway::CHANGE_PASSWORD_RESP);
  EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(bad.error_message(), "Invalid request");

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  const std::vector<std::optional<std::string>> row = {
      "5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"};

  // 旧口令错：AUTH_FAILED + 固定文案。
  fake_mysql::PushRows({row});
  chirp::auth::ChangePasswordRequest wrong;
  wrong.set_user_id("user_1");
  wrong.set_old_password("Wrong!old1");
  wrong.set_new_password("Str0ng!pass2");
  auto denied_session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::CHANGE_PASSWORD_REQ, wrong, denied_session);
  auto denied = LastResponse<chirp::auth::ChangePasswordResponse>(
      *denied_session, chirp::gateway::CHANGE_PASSWORD_RESP);
  EXPECT_EQ(denied.code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(denied.error_message(), "Failed to change password. Check your old password.");

  // Happy：改密成功 → OK、无错误文案。
  fake_mysql::PushRows({row});
  chirp::auth::ChangePasswordRequest req;
  req.set_user_id("user_1");
  req.set_old_password("Str0ng!pass");
  req.set_new_password("Str0ng!pass2");
  auto happy = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::CHANGE_PASSWORD_REQ, req, happy, 5);
  auto ok = LastResponse<chirp::auth::ChangePasswordResponse>(
      *happy, chirp::gateway::CHANGE_PASSWORD_RESP);
  EXPECT_EQ(ok.code(), chirp::common::OK);
  EXPECT_TRUE(ok.error_message().empty());
  EXPECT_EQ(FramesOf(*happy, chirp::gateway::CHANGE_PASSWORD_RESP)[0].sequence(), 5);
}

TEST_F(AuthEnhancedMainTest, HeartbeatStampsServerTimeAndUnknownIsSilent) {
  // PONG 双字段都盖服务器时间（不回显客户端 timestamp），sequence 回带。
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(123456);
  auto session = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::HEARTBEAT_PING, ping, session, 7);
  const auto pongs = FramesOf(*session, chirp::gateway::HEARTBEAT_PONG);
  ASSERT_EQ(pongs.size(), 1u);
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(pong.ParseFromString(pongs[0].body()));
  EXPECT_GT(pong.timestamp(), 123456);  // 服务器时间戳（1970 纪元），非回显
  EXPECT_GT(pong.server_time(), 0);
  EXPECT_EQ(pongs[0].sequence(), 7);

  // 未知 msg_id：default 分支静默，不回帧。
  auto stranger = std::make_shared<MockSession>();
  Dispatch(static_cast<chirp::gateway::MsgID>(9999), "", stranger);
  EXPECT_TRUE(stranger->sent.empty());
}

}  // namespace
