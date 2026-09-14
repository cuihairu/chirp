// Unit tests for the enhanced auth storage layer: UserStore (MySQL),
// SessionStore (MySQL) and RedisAuthStore (Redis), using the scripted fake
// MySQL C API and the in-memory fake Redis server.

#include <gtest/gtest.h>

#include <asio.hpp>

#include "fake_mysql.h"
#include "fake_servers.h"
#include "in_memory_redis.h"
#include "password_hasher.h"
#include "redis_auth_store.h"
#include "session_store.h"
#include "user_store.h"

namespace chirp_test {
// Provided by fake_sodium.cc (linked into this test target).
namespace fake_sodium {
void Reset();
void SetPwhashShouldFail(bool);
}
}  // namespace chirp_test

namespace {

using chirp::auth::RedisAuthStore;
using chirp::auth::SessionStore;
using chirp::auth::UserStore;
namespace fake_mysql = chirp_test::fake_mysql;
namespace fake_sodium = chirp_test::fake_sodium;

UserStore::Config DefaultUserConfig() {
  UserStore::Config cfg;
  cfg.pool_size = 2;
  return cfg;
}

SessionStore::Config DefaultSessionConfig() {
  SessionStore::Config cfg;
  cfg.pool_size = 2;
  return cfg;
}

// A 9-column users row as expected by UserStore::FindBy*. Any column may be
// nulled via the placeholders.
std::vector<std::optional<std::string>> UserRow(
    const std::string& user_id,
    const std::string& username,
    std::optional<std::string> email,
    const std::string& hash,
    const std::string& active = "1") {
  return {"7", user_id, username, std::move(email), hash, "100", "200", "300", active};
}

class UserStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { fake_mysql::Reset(); }
};

TEST_F(UserStoreTest, InitializeFailsWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.Initialize());
  EXPECT_EQ(fake_mysql::LiveHandles(), 0);
}

TEST_F(UserStoreTest, InitializeFailsOnQueryError) {
  fake_mysql::PushQueryError("boom");
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.Initialize());
}

TEST_F(UserStoreTest, InitializeWarnsWhenTableMissingButSucceeds) {
  fake_mysql::PushRows({});  // SHOW TABLES returns no rows
  UserStore store(DefaultUserConfig());
  EXPECT_TRUE(store.Initialize());
}

TEST_F(UserStoreTest, InitializeSucceedsWhenTablePresent) {
  fake_mysql::PushRows({{"users"}});
  UserStore store(DefaultUserConfig());
  EXPECT_TRUE(store.Initialize());
}

TEST_F(UserStoreTest, RegisterRejectsWeakPasswordBeforeTouchingDb) {
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "short";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::INVALID_PARAM);
  EXPECT_TRUE(fake_mysql::TakeQueries().empty());
}

TEST_F(UserStoreTest, RegisterRejectsExistingUsername) {
  fake_mysql::PushRows({{"1"}});  // COUNT(*) -> username exists
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("Username"), std::string::npos);
}

TEST_F(UserStoreTest, RegisterRejectsExistingEmail) {
  fake_mysql::PushRows({{"0"}});  // username free
  fake_mysql::PushRows({{"1"}});  // email exists
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.email = "a@b.c";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("Email"), std::string::npos);
}

TEST_F(UserStoreTest, RegisterSucceedsAndWritesEscapedInsert) {
  fake_mysql::PushRows({{"0"}});  // username free
  fake_mysql::PushRows({{"0"}});  // email free
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "ali'ce";
  req.email = "a@b.c";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::OK);
  EXPECT_FALSE(result.user_id.empty());

  auto queries = fake_mysql::TakeQueries();
  ASSERT_FALSE(queries.empty());
  EXPECT_NE(queries.back().find("INSERT INTO users"), std::string::npos);
  EXPECT_NE(queries.back().find("ali\\'ce"), std::string::npos);  // escaped
}

TEST_F(UserStoreTest, RegisterFailsWhenInsertErrors) {
  fake_mysql::PushRows({{"0"}});
  fake_mysql::PushRows({{"0"}});
  fake_mysql::FailQueriesMatching("INSERT INTO users");
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::INTERNAL_ERROR);
}

TEST_F(UserStoreTest, RegisterFailsWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::INTERNAL_ERROR);
}

TEST_F(UserStoreTest, RegisterFailsWhenPasswordHashingFails) {
  fake_mysql::PushRows({{"0"}});
  fake_mysql::PushRows({{"0"}});
  chirp_test::fake_sodium::SetPwhashShouldFail(true);
  UserStore store(DefaultUserConfig());
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "Str0ng!pass";
  auto result = store.Register(req);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::INTERNAL_ERROR);
  chirp_test::fake_sodium::Reset();
}

TEST_F(UserStoreTest, FindByUserIdParsesRowAndHandlesNullColumns) {
  auto row = UserRow("user_1", "alice", std::nullopt, "$fake$00$");
  fake_mysql::PushRows({row});
  UserStore store(DefaultUserConfig());
  auto user = store.FindByUserId("user_1");
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->id, 7);
  EXPECT_EQ(user->user_id, "user_1");
  EXPECT_EQ(user->username, "alice");
  EXPECT_EQ(user->email, "");       // NULL column
  EXPECT_EQ(user->password_hash, "$fake$00$");
  EXPECT_TRUE(user->is_active);
}

TEST_F(UserStoreTest, FindByUserIdFailsOnQueryError) {
  fake_mysql::PushQueryError("bad sql");
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.FindByUserId("user_1").has_value());
}

TEST_F(UserStoreTest, FindByUserIdFailsWithoutResult) {
  fake_mysql::SetStoreResultShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.FindByUserId("user_1").has_value());
}

TEST_F(UserStoreTest, FindByUserIdFailsWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.FindByUserId("user_1").has_value());
}

TEST_F(UserStoreTest, FindByUsernameInactiveRowAndEmptyResult) {
  fake_mysql::PushRows({UserRow("user_1", "alice", "a@b.c", "h", "0")});
  UserStore store(DefaultUserConfig());
  auto user = store.FindByUsername("alice");
  ASSERT_TRUE(user.has_value());
  EXPECT_FALSE(user->is_active);

  fake_mysql::PushRows({});  // no rows
  EXPECT_FALSE(store.FindByUsername("bob").has_value());
}

TEST_F(UserStoreTest, FindByEmailSucceedsAndHandlesFailures) {
  fake_mysql::PushRows({UserRow("user_1", "alice", "a@b.c", "h")});
  UserStore store(DefaultUserConfig());
  EXPECT_TRUE(store.FindByEmail("a@b.c").has_value());

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.FindByEmail("a@b.c").has_value());
}

TEST_F(UserStoreTest, VerifyCredentialsPrefersUsernameThenEmailThenFails) {
  const std::string hash = chirp::auth::PasswordHasher::HashPassword("Str0ng!pass");

  // Found by username with matching password.
  fake_mysql::PushRows({UserRow("u1", "alice", "a@b.c", hash)});
  UserStore store(DefaultUserConfig());
  auto user = store.VerifyCredentials("alice", "Str0ng!pass");
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->username, "alice");

  // Not found by username but found by email.
  fake_mysql::PushRows({});  // username miss
  fake_mysql::PushRows({UserRow("u2", "bob", "b@b.c", hash)});
  user = store.VerifyCredentials("b@b.c", "Str0ng!pass");
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->user_id, "u2");

  // Unknown identifier.
  fake_mysql::PushRows({});
  fake_mysql::PushRows({});
  EXPECT_FALSE(store.VerifyCredentials("ghost", "Str0ng!pass").has_value());

  // Inactive account.
  fake_mysql::PushRows({UserRow("u1", "alice", "a@b.c", hash, "0")});
  EXPECT_FALSE(store.VerifyCredentials("alice", "Str0ng!pass").has_value());

  // Wrong password.
  fake_mysql::PushRows({UserRow("u1", "alice", "a@b.c", hash)});
  EXPECT_FALSE(store.VerifyCredentials("alice", "Wrong!pass1").has_value());
}

TEST_F(UserStoreTest, UpdateWriteOperationsCoverFailureAndSuccess) {
  UserStore store(DefaultUserConfig());
  EXPECT_TRUE(store.UpdateLastLogin("u1", 123));
  EXPECT_TRUE(store.ChangePassword("u1", "newhash"));
  EXPECT_TRUE(store.SetActiveStatus("u1", false));

  fake_mysql::PushQueryError("nope");
  EXPECT_FALSE(store.UpdateLastLogin("u1", 123));
}

TEST_F(UserStoreTest, UpdateWriteOperationsFailWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.UpdateLastLogin("u1", 1));
  EXPECT_FALSE(store.ChangePassword("u1", "h"));
  EXPECT_FALSE(store.SetActiveStatus("u1", true));
}

TEST_F(UserStoreTest, ExistenceChecksCoverAllBranches) {
  UserStore store(DefaultUserConfig());

  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.UsernameExists("alice"));

  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.EmailExists("a@b.c"));

  fake_mysql::PushRows({{"0"}});
  EXPECT_FALSE(store.UsernameExists("bob"));

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.UsernameExists("bob"));

  fake_mysql::SetStoreResultShouldFail(true);
  EXPECT_FALSE(store.EmailExists("a@b.c"));
  fake_mysql::SetStoreResultShouldFail(false);
}

TEST_F(UserStoreTest, ExistenceChecksFailWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.UsernameExists("alice"));
  EXPECT_FALSE(store.EmailExists("a@b.c"));
}

TEST_F(UserStoreTest, GetActiveSessionCountBranches) {
  UserStore store(DefaultUserConfig());

  fake_mysql::PushRows({{"3"}});
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 3);

  fake_mysql::PushRows({std::vector<std::optional<std::string>>{std::nullopt}});
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);

  fake_mysql::PushRows({});
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);

  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);

  fake_mysql::SetConnectShouldFail(true);
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);
}

TEST_F(UserStoreTest, PooledConnectionReusesLiveHandleAndDropsStaleOne) {
  UserStore store(DefaultUserConfig());
  // First call opens a fresh handle; returning it puts it in the pool.
  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.Initialize());
  EXPECT_EQ(fake_mysql::LiveHandles(), 1);

  // The pooled handle fails ping and is replaced by a new connection.
  fake_mysql::SetPingShouldFail(true);
  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.Initialize());
}

// ---------------------------------------------------------------------------
// SessionStore
// ---------------------------------------------------------------------------

class SessionStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { fake_mysql::Reset(); }
};

TEST_F(SessionStoreTest, InitializeRequiresConnection) {
  fake_mysql::SetConnectShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_FALSE(store.Initialize());
}

TEST_F(SessionStoreTest, InitializeSucceedsWithConnection) {
  SessionStore store(DefaultSessionConfig());
  EXPECT_TRUE(store.Initialize());
}

TEST_F(SessionStoreTest, CreateSessionRoundTripAndFailures) {
  SessionStore store(DefaultSessionConfig());

  chirp::auth::CreateSessionRequest req;
  req.user_id = "u1";
  req.device_id = "d1";
  req.platform = "pc";
  req.ttl_seconds = 60;
  auto session = store.CreateSession(req);
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->user_id, "u1");
  EXPECT_EQ(session->device_id, "d1");
  EXPECT_FALSE(session->session_id.empty());
  EXPECT_TRUE(session->is_active);
  EXPECT_GT(session->expires_at, session->created_at);

  fake_mysql::FailQueriesMatching("INSERT INTO sessions");
  EXPECT_FALSE(store.CreateSession(req).has_value());

  fake_mysql::SetConnectShouldFail(true);
  EXPECT_FALSE(store.CreateSession(req).has_value());
}

TEST_F(SessionStoreTest, GetSessionParsesRow) {
  std::vector<std::optional<std::string>> row = {
      "9", "sess_1", "u1", "d1", "pc", "10", "99999999999", "20", "1"};
  fake_mysql::PushRows({row});
  SessionStore store(DefaultSessionConfig());
  auto session = store.GetSession("sess_1");
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->id, 9);
  EXPECT_EQ(session->session_id, "sess_1");
  EXPECT_EQ(session->platform, "pc");
  EXPECT_TRUE(session->is_active);

  fake_mysql::PushRows({});  // no such session
  EXPECT_FALSE(store.GetSession("sess_1").has_value());

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.GetSession("sess_1").has_value());

  fake_mysql::SetStoreResultShouldFail(true);
  EXPECT_FALSE(store.GetSession("sess_1").has_value());
  fake_mysql::SetStoreResultShouldFail(false);
}

TEST_F(SessionStoreTest, GetSessionFailsWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_FALSE(store.GetSession("s").has_value());
}

TEST_F(SessionStoreTest, GetUserSessionsParsesMultipleRows) {
  std::vector<std::optional<std::string>> r1 = {"1", "s1", "u1", "d", "pc",
                                                "1", "2", "3", "1"};
  std::vector<std::optional<std::string>> r2 = {"2", "s2", "u1", std::nullopt, "web",
                                                "1", "2", "3", "0"};
  fake_mysql::PushRows({r1, r2});
  SessionStore store(DefaultSessionConfig());
  auto sessions = store.GetUserSessions("u1");
  ASSERT_EQ(sessions.size(), 2u);
  EXPECT_EQ(sessions[0].session_id, "s1");
  EXPECT_EQ(sessions[1].device_id, "");   // NULL column
  EXPECT_FALSE(sessions[1].is_active);

  fake_mysql::PushQueryError("x");
  EXPECT_TRUE(store.GetUserSessions("u1").empty());
}

TEST_F(SessionStoreTest, UpdateActivityAndRevokeBranches) {
  SessionStore store(DefaultSessionConfig());
  EXPECT_TRUE(store.UpdateSessionActivity("s1", 5));
  EXPECT_TRUE(store.RevokeSession("s1"));

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.UpdateSessionActivity("s1", 5));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.RevokeSession("s1"));
}

TEST_F(SessionStoreTest, UpdateActivityAndRevokeFailWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_FALSE(store.UpdateSessionActivity("s1", 5));
  EXPECT_FALSE(store.RevokeSession("s1"));
}

TEST_F(SessionStoreTest, RevokeFamilyUsesAffectedRows) {
  SessionStore store(DefaultSessionConfig());

  fake_mysql::SetAffectedRows(2);
  EXPECT_EQ(store.RevokeOtherSessions("u1", "keep"), 2);

  fake_mysql::SetAffectedRows(3);
  EXPECT_EQ(store.RevokeAllUserSessions("u1"), 3);

  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.RevokeAllUserSessions("u1"), 0);

  fake_mysql::SetAffectedRows(5);
  EXPECT_EQ(store.CleanupExpiredSessions(), 5);

  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.CleanupExpiredSessions(), 0);
}

TEST_F(SessionStoreTest, RevokeFamilyFailsWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_EQ(store.RevokeOtherSessions("u1", "keep"), 0);
  EXPECT_EQ(store.RevokeAllUserSessions("u1"), 0);
  EXPECT_EQ(store.CleanupExpiredSessions(), 0);
}

TEST_F(SessionStoreTest, RefreshTokenLifecycle) {
  SessionStore store(DefaultSessionConfig());

  chirp::auth::CreateRefreshTokenRequest req;
  req.user_id = "u1";
  req.session_id = "s1";
  req.device_id = "d1";
  req.ttl_seconds = 100;
  auto token = store.CreateRefreshToken(req, "hash-of-token");
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->user_id, "u1");
  EXPECT_EQ(token->session_id, "s1");
  EXPECT_FALSE(token->token_id.empty());
  EXPECT_GT(token->expires_at, token->created_at);

  fake_mysql::FailQueriesMatching("INSERT INTO refresh_tokens");
  EXPECT_FALSE(store.CreateRefreshToken(req, "h2").has_value());

  fake_mysql::SetConnectShouldFail(true);
  EXPECT_FALSE(store.CreateRefreshToken(req, "h3").has_value());
}

TEST_F(SessionStoreTest, GetRefreshTokenParsesRow) {
  std::vector<std::optional<std::string>> row = {
      "1", "tok_1", "u1", "s1", "d1", "storedhash", "10", "99999999999", "0", "0"};
  fake_mysql::PushRows({row});
  SessionStore store(DefaultSessionConfig());
  auto token = store.GetRefreshToken("tok_1");
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->token_hash, "storedhash");
  EXPECT_FALSE(token->is_revoked);

  fake_mysql::PushRows({});
  EXPECT_FALSE(store.GetRefreshToken("tok_1").has_value());
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.GetRefreshToken("tok_1").has_value());
}

TEST_F(SessionStoreTest, VerifyRefreshTokenParsesRowAndHandlesMisses) {
  // (id, token_id, user, session, device, hash, created, expires, revoked_at, is_revoked)
  const std::vector<std::optional<std::string>> row = {
      "1", "t1", "u1", "s1", "d1", "myhash", "10", "99999999999", "0", "0"};

  SessionStore store(DefaultSessionConfig());
  fake_mysql::PushRows({row});
  auto token = store.VerifyRefreshToken("myhash");
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->user_id, "u1");
  EXPECT_FALSE(token->is_revoked);

  fake_mysql::PushRows({});  // no match (revoked/expired tokens are filtered in SQL)
  EXPECT_FALSE(store.VerifyRefreshToken("nope").has_value());

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.VerifyRefreshToken("myhash").has_value());
}

TEST_F(SessionStoreTest, RefreshTokenRevocationAndCleanup) {
  SessionStore store(DefaultSessionConfig());

  EXPECT_TRUE(store.RevokeRefreshToken("tok_1"));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.RevokeRefreshToken("tok_1"));

  fake_mysql::SetAffectedRows(4);
  EXPECT_EQ(store.RevokeAllUserRefreshTokens("u1"), 4);
  EXPECT_EQ(store.RevokeSessionRefreshTokens("s1"), 4);
  EXPECT_EQ(store.CleanupExpiredRefreshTokens(), 4);

  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.RevokeAllUserRefreshTokens("u1"), 0);
  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.RevokeSessionRefreshTokens("s1"), 0);
  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.CleanupExpiredRefreshTokens(), 0);
}

TEST_F(SessionStoreTest, RefreshTokenOpsFailWithoutConnection) {
  fake_mysql::SetConnectShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_FALSE(store.RevokeRefreshToken("t"));
  EXPECT_EQ(store.RevokeAllUserRefreshTokens("u1"), 0);
  EXPECT_EQ(store.RevokeSessionRefreshTokens("s1"), 0);
  EXPECT_EQ(store.CleanupExpiredRefreshTokens(), 0);
  EXPECT_FALSE(store.GetRefreshToken("t").has_value());
  EXPECT_FALSE(store.VerifyRefreshToken("h").has_value());
  EXPECT_FALSE(store.CheckSessionLimit("u1", 5));
}

TEST_F(SessionStoreTest, CheckSessionLimitComparesAgainstCount) {
  SessionStore store(DefaultSessionConfig());

  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.CheckSessionLimit("u1", 5));   // under limit

  fake_mysql::PushRows({{"5"}});
  EXPECT_FALSE(store.CheckSessionLimit("u1", 5));  // at limit

  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.CheckSessionLimit("u1", 5));  // be safe on error
}

// ---------------------------------------------------------------------------
// RedisAuthStore (against the in-memory fake Redis)
// ---------------------------------------------------------------------------

class RedisAuthStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    redis_.Clear();
    // RedisAuthStore::Connect() probes with GET ping.
    redis_.SetDirect("ping", "pong");
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_.Handle(args); });
    RedisAuthStore::Config cfg;
    cfg.port = fake_->port();
    store_ = std::make_unique<RedisAuthStore>(io_, cfg);
    ASSERT_TRUE(store_->Connect());
    EXPECT_TRUE(store_->IsConnected());
  }

  void TearDown() override { fake_.reset(); }

  asio::io_context io_;
  chirp_test::InMemoryRedis redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::unique_ptr<RedisAuthStore> store_;
};

TEST_F(RedisAuthStoreTest, ConnectFailureReportsDisconnected) {
  // A store pointed at a dead port cannot ping.
  RedisAuthStore::Config cfg;
  cfg.port = 1;  // nothing listens here
  RedisAuthStore store(io_, cfg);
  EXPECT_FALSE(store.Connect());
  EXPECT_FALSE(store.IsConnected());
}

TEST_F(RedisAuthStoreTest, SessionLifecycle) {
  EXPECT_TRUE(store_->StoreSession("s1", "u1", "d1", "pc", INT64_MAX));
  EXPECT_EQ(store_->GetSessionUser("s1").value_or(""), "u1");
  EXPECT_TRUE(store_->UpdateSessionActivity("s1", 5));
  EXPECT_TRUE(store_->DeleteSession("s1"));
  EXPECT_FALSE(store_->GetSessionUser("s1").has_value());
}

TEST_F(RedisAuthStoreTest, StoreSessionWithExpiredTtlUsesDefault) {
  // expires_at in the past -> falls back to the configured session TTL for
  // the key's lifetime, but GetSessionUser still rejects the expired value.
  EXPECT_TRUE(store_->StoreSession("s2", "u1", "", "", 1));
  EXPECT_FALSE(store_->GetSessionUser("s2").has_value());
}

TEST_F(RedisAuthStoreTest, StoreSessionFailsWhenDisconnected) {
  store_->Disconnect();  // via Shutdown-like path
  EXPECT_FALSE(store_->StoreSession("s9", "u1", "d", "pc", 1));
}

TEST_F(RedisAuthStoreTest, DeleteAllUserSessionsRemovesListedIds) {
  store_->StoreSession("a", "u1", "d", "pc", INT64_MAX);
  store_->StoreSession("b", "u1", "d", "pc", INT64_MAX);
  EXPECT_GT(store_->DeleteAllUserSessions("u1"), 0);
  EXPECT_FALSE(store_->GetSessionUser("a").has_value());
  EXPECT_FALSE(store_->GetSessionUser("b").has_value());
}

TEST_F(RedisAuthStoreTest, RefreshTokenLifecycle) {
  EXPECT_TRUE(store_->StoreRefreshToken("t1", "u1", "s1", INT64_MAX));
  EXPECT_EQ(store_->GetRefreshTokenUser("t1").value_or(""), "u1");
  EXPECT_TRUE(store_->DeleteRefreshToken("t1"));
  EXPECT_FALSE(store_->GetRefreshTokenUser("t1").has_value());
}

TEST_F(RedisAuthStoreTest, RateLimitWindowBehavior) {
  const std::string key = "login:1.2.3.4";
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(store_->CheckRateLimit(key, 3));
  }
  EXPECT_EQ(store_->GetRateLimitCount(key), 3);
  EXPECT_FALSE(store_->CheckRateLimit(key, 3));  // 4th attempt denied
  EXPECT_TRUE(store_->ResetRateLimit(key));
  EXPECT_EQ(store_->GetRateLimitCount(key), 0);
  EXPECT_TRUE(store_->CheckRateLimit(key, 3));
}

TEST_F(RedisAuthStoreTest, FailedLoginTracking) {
  EXPECT_TRUE(store_->RecordFailedLogin("alice", "1.1.1.1"));
  EXPECT_TRUE(store_->RecordFailedLogin("alice", "1.1.1.1"));
  EXPECT_EQ(store_->GetFailedLoginCount("alice"), 2);
  EXPECT_TRUE(store_->ClearFailedLogins("alice"));
  EXPECT_EQ(store_->GetFailedLoginCount("alice"), 0);
}

TEST_F(RedisAuthStoreTest, AccountLocking) {
  EXPECT_TRUE(store_->LockAccount("u1", 60));
  EXPECT_TRUE(store_->IsAccountLocked("u1"));
  EXPECT_TRUE(store_->UnlockAccount("u1"));
  EXPECT_FALSE(store_->IsAccountLocked("u1"));
}

TEST_F(RedisAuthStoreTest, DeviceRegistry) {
  store_->StoreSession("s1", "u1", "phone", "ios", INT64_MAX);
  store_->StoreSession("s2", "u1", "pc", "windows", INT64_MAX);
  auto devices = store_->GetUserDevices("u1");
  EXPECT_EQ(devices.size(), 2u);
  EXPECT_TRUE(store_->RemoveDevice("u1", "phone"));
}

TEST_F(RedisAuthStoreTest, DisconnectedStoreRejectsEveryOperation) {
  // A store that was never Connect()ed must fail closed for every call.
  RedisAuthStore::Config cfg;
  cfg.port = fake_->port();
  RedisAuthStore disconnected(io_, cfg);
  EXPECT_FALSE(disconnected.IsConnected());

  EXPECT_FALSE(disconnected.StoreSession("s1", "u1", "d1", "web", INT64_MAX));
  EXPECT_FALSE(disconnected.GetSessionUser("s1").has_value());
  EXPECT_FALSE(disconnected.UpdateSessionActivity("s1", 1));
  EXPECT_FALSE(disconnected.DeleteSession("s1"));
  EXPECT_EQ(disconnected.DeleteAllUserSessions("u1"), 0);
  EXPECT_FALSE(disconnected.StoreRefreshToken("t1", "u1", "s1", INT64_MAX));
  EXPECT_FALSE(disconnected.GetRefreshTokenUser("t1").has_value());
  EXPECT_FALSE(disconnected.DeleteRefreshToken("t1"));
  EXPECT_TRUE(disconnected.CheckRateLimit("k", 3));  // fail-open by design
  EXPECT_EQ(disconnected.GetRateLimitCount("k"), 0);
  EXPECT_FALSE(disconnected.ResetRateLimit("k"));
  EXPECT_FALSE(disconnected.RecordFailedLogin("alice", "1.1.1.1"));
  EXPECT_EQ(disconnected.GetFailedLoginCount("alice"), 0);
  EXPECT_FALSE(disconnected.ClearFailedLogins("alice"));
  EXPECT_FALSE(disconnected.IsAccountLocked("u1"));
  EXPECT_FALSE(disconnected.LockAccount("u1", 60));
  EXPECT_FALSE(disconnected.UnlockAccount("u1"));
  EXPECT_TRUE(disconnected.GetUserDevices("u1").empty());
  EXPECT_FALSE(disconnected.RemoveDevice("u1", "d1"));
}

TEST_F(RedisAuthStoreTest, ConnectedEdgeCases) {
  ASSERT_TRUE(store_->Connect());

  // Activity update for a session Redis does not know about.
  EXPECT_FALSE(store_->UpdateSessionActivity("missing", 1));

  // A stored value missing its '|' separators fails to parse.
  redis_.SetDirect("chirp:auth:session:bad", "garbage");
  EXPECT_FALSE(store_->UpdateSessionActivity("bad", 1));

  // An expiry already in the past falls back to the configured token TTL.
  EXPECT_TRUE(store_->StoreRefreshToken("t1", "u1", "s1", 1000));

  // A stored refresh token whose expiry has passed resolves to nothing.
  redis_.SetDirect("chirp:auth:refresh_token:t2", "u1|s1|1000");
  EXPECT_FALSE(store_->GetRefreshTokenUser("t2").has_value());
}

// ---------------------------------------------------------------------------
// MySQL error-path sweeps: every query failure, failed store_result and
// broken-pool state must surface as the method's documented default.
// ---------------------------------------------------------------------------

TEST_F(UserStoreTest, InitFailureGuardsReturnDefaults) {
  fake_mysql::SetInitShouldFail(true);
  UserStore store(DefaultUserConfig());
  EXPECT_FALSE(store.Initialize());
  EXPECT_FALSE(store.FindByUsername("alice").has_value());
  EXPECT_FALSE(store.FindByEmail("a@b.c").has_value());
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);
}

TEST_F(UserStoreTest, QueryErrorPathsReturnDefaults) {
  UserStore store(DefaultUserConfig());
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.FindByUsername("alice").has_value());
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.FindByEmail("a@b.c").has_value());
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.EmailExists("a@b.c"));
  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);
}

TEST_F(UserStoreTest, StoreResultFailurePathsReturnDefaults) {
  UserStore store(DefaultUserConfig());
  fake_mysql::SetStoreResultShouldFail(true);
  EXPECT_FALSE(store.FindByUsername("alice").has_value());
  EXPECT_FALSE(store.FindByEmail("a@b.c").has_value());
  EXPECT_FALSE(store.UsernameExists("bob"));
  EXPECT_EQ(store.GetActiveSessionCount("u1"), 0);
}

TEST_F(UserStoreTest, SurplusConnectionsAreClosedNotPooled) {
  UserStore::Config cfg;
  cfg.pool_size = 0;  // nothing ever parks in the pool
  UserStore store(cfg);
  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.UsernameExists("alice"));
  EXPECT_EQ(fake_mysql::LiveHandles(), 0);
}

TEST_F(SessionStoreTest, InitFailureGuardsReturnDefaults) {
  fake_mysql::SetInitShouldFail(true);
  SessionStore store(DefaultSessionConfig());
  EXPECT_FALSE(store.Initialize());

  chirp::auth::CreateSessionRequest req;
  req.user_id = "u1";
  req.ttl_seconds = 60;
  EXPECT_FALSE(store.CreateSession(req).has_value());
  EXPECT_TRUE(store.GetUserSessions("u1").empty());

  chirp::auth::CreateRefreshTokenRequest token_req;
  token_req.user_id = "u1";
  token_req.ttl_seconds = 60;
  EXPECT_FALSE(store.CreateRefreshToken(token_req, "hash").has_value());
}

TEST_F(SessionStoreTest, QueryAndStoreResultFailuresReturnDefaults) {
  SessionStore store(DefaultSessionConfig());

  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.RevokeOtherSessions("u1", "keep"), 0);

  fake_mysql::SetStoreResultShouldFail(true);
  EXPECT_TRUE(store.GetUserSessions("u1").empty());
  EXPECT_FALSE(store.GetRefreshToken("tok_1").has_value());
  EXPECT_FALSE(store.VerifyRefreshToken("hash").has_value());
  EXPECT_EQ(store.RevokeAllUserSessions("u1"), 0);
  EXPECT_EQ(store.CleanupExpiredSessions(), 0);
  EXPECT_EQ(store.RevokeAllUserRefreshTokens("u1"), 0);
  EXPECT_EQ(store.RevokeSessionRefreshTokens("s1"), 0);
  EXPECT_FALSE(store.CheckSessionLimit("u1", 5));
}

TEST_F(SessionStoreTest, PooledConnectionPingFailureReopens) {
  SessionStore store(DefaultSessionConfig());
  ASSERT_TRUE(store.GetUserSessions("u1").empty());  // parks a connection
  EXPECT_EQ(fake_mysql::LiveHandles(), 1);

  fake_mysql::SetPingShouldFail(true);
  ASSERT_TRUE(store.GetUserSessions("u1").empty());  // dead pooled conn replaced
  EXPECT_EQ(fake_mysql::LiveHandles(), 1);
}

TEST_F(SessionStoreTest, SurplusConnectionsAreClosedNotPooled) {
  SessionStore::Config cfg;
  cfg.pool_size = 0;
  SessionStore store(cfg);
  ASSERT_TRUE(store.GetUserSessions("u1").empty());
  EXPECT_EQ(fake_mysql::LiveHandles(), 0);
}

}  // namespace
