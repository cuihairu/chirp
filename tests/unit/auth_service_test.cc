// Unit tests for the enhanced auth components: PasswordHasher and
// TokenGenerator (fake libsodium), RateLimiter and BruteForceProtector
// (fake Redis via in-memory server), and the orchestrating AuthService
// (fake MySQL + fake Redis).

#include <gtest/gtest.h>

#include <asio.hpp>

#include "auth_service.h"
#include "brute_force_protector.h"
#include "fake_mysql.h"
#include "fake_servers.h"
#include "in_memory_redis.h"
#include "jwt.h"
#include "password_hasher.h"
#include "rate_limiter.h"
#include "redis_auth_store.h"
#include "session_store.h"
#include "token_generator.h"
#include "user_store.h"

namespace chirp_test {
// Provided by fake_sodium.cc (linked into this test target).
namespace fake_sodium {
void Reset();
void SetInitShouldFail(bool);
void SetPwhashShouldFail(bool);
}
}  // namespace chirp_test

namespace {

using chirp::auth::AuthService;
using chirp::auth::BruteForceProtector;
using chirp::auth::PasswordHasher;
using chirp::auth::RateLimiter;
using chirp::auth::RedisAuthStore;
using chirp::auth::TokenGenerator;
namespace fake_mysql = chirp_test::fake_mysql;
namespace fake_sodium = chirp_test::fake_sodium;

// ---------------------------------------------------------------------------
// PasswordHasher (fake sodium)
// ---------------------------------------------------------------------------

class PasswordHasherTest : public ::testing::Test {
 protected:
  void SetUp() override { fake_sodium::Reset(); }
};

TEST_F(PasswordHasherTest, HashAndVerifyRoundTrip) {
  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  EXPECT_FALSE(hash.empty());
  EXPECT_TRUE(PasswordHasher::VerifyPassword("Str0ng!pass", hash));
  EXPECT_FALSE(PasswordHasher::VerifyPassword("Other!pass1", hash));
  EXPECT_FALSE(PasswordHasher::VerifyPassword("Str0ng!pass", "$garbage$"));
}

TEST_F(PasswordHasherTest, HashFailsWhenSodiumInitFails) {
  fake_sodium::SetInitShouldFail(true);
  EXPECT_TRUE(PasswordHasher::HashPassword("Str0ng!pass").empty());
  EXPECT_FALSE(PasswordHasher::VerifyPassword("Str0ng!pass", "x"));
  EXPECT_TRUE(PasswordHasher::GenerateSalt().empty());
}

TEST_F(PasswordHasherTest, HashFailsWhenPwhashFails) {
  fake_sodium::SetPwhashShouldFail(true);
  EXPECT_TRUE(PasswordHasher::HashPassword("Str0ng!pass").empty());
}

TEST_F(PasswordHasherTest, StrengthValidation) {
  EXPECT_NE(PasswordHasher::ValidateStrength("short"), "");           // too short
  EXPECT_NE(PasswordHasher::ValidateStrength("aaaa1111"), "");        // single class
  EXPECT_NE(PasswordHasher::ValidateStrength("AaBbCc"), "");          // two classes
  // Three or more character classes pass (score threshold is 3 of 4).
  EXPECT_EQ(PasswordHasher::ValidateStrength("alllowercase1!"), "");  // lower+digit+special
  EXPECT_EQ(PasswordHasher::ValidateStrength("NOLOWERCASE1!"), "");   // upper+digit+special
  EXPECT_EQ(PasswordHasher::ValidateStrength("NoDigits!!!"), "");     // upper+lower+special
  EXPECT_EQ(PasswordHasher::ValidateStrength("NoSpecial123"), "");    // upper+lower+digit
  EXPECT_EQ(PasswordHasher::ValidateStrength("Str0ng!pass"), "");     // all four
}

TEST_F(PasswordHasherTest, SaltIsBase64OfRandomBytes) {
  const std::string salt = PasswordHasher::GenerateSalt();
  EXPECT_FALSE(salt.empty());
  EXPECT_NE(salt, PasswordHasher::GenerateSalt());  // deterministic xorshift advances
}

TEST_F(PasswordHasherTest, ConfigAccessors) {
  PasswordHasher::Config cfg;
  cfg.time_cost = 4;
  PasswordHasher::SetConfig(cfg);
  EXPECT_EQ(PasswordHasher::DefaultConfig().time_cost, 4);
  PasswordHasher::SetConfig(PasswordHasher::Config{});  // restore
}

// ---------------------------------------------------------------------------
// TokenGenerator
// ---------------------------------------------------------------------------

class TokenGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override { fake_sodium::Reset(); }
};

TEST_F(TokenGeneratorTest, TokenIdHasExpectedLengthAndUniqueness) {
  EXPECT_EQ(TokenGenerator::GenerateTokenId(16).size(), 32u);  // hex
  EXPECT_EQ(TokenGenerator::GenerateTokenId().size(), 64u);    // default 32 bytes
  EXPECT_NE(TokenGenerator::GenerateTokenId(8), TokenGenerator::GenerateTokenId(8));
}

TEST_F(TokenGeneratorTest, TokenIdFallsBackToStdRandomWhenSodiumUnavailable) {
  fake_sodium::SetInitShouldFail(true);
  const std::string token = TokenGenerator::GenerateTokenId(8);
  EXPECT_EQ(token.size(), 16u);  // still hex-doubled
}

TEST_F(TokenGeneratorTest, PrefixedTokensAndDeterministicHashes) {
  EXPECT_TRUE(TokenGenerator::GenerateSessionId("alice").rfind("sess_", 0) == 0);
  EXPECT_TRUE(TokenGenerator::GenerateRefreshToken().rfind("rt_", 0) == 0);
  EXPECT_TRUE(TokenGenerator::GenerateResetToken().rfind("reset_", 0) == 0);

  EXPECT_EQ(TokenGenerator::HashDeviceId("device", "ios"),
            TokenGenerator::HashDeviceId("device", "ios"));
  EXPECT_NE(TokenGenerator::HashDeviceId("device", "ios"),
            TokenGenerator::HashDeviceId("device", "android"));

  const std::string hash = TokenGenerator::HashToken("secret-token");
  EXPECT_EQ(hash.size(), 64u);
  EXPECT_TRUE(TokenGenerator::VerifyTokenHash("secret-token", hash));
  EXPECT_FALSE(TokenGenerator::VerifyTokenHash("other-token", hash));
}

TEST_F(TokenGeneratorTest, AccessTokenIsVerifiableJwt) {
  const std::string jwt = TokenGenerator::GenerateAccessToken("user_1", "s3cret", 60);
  chirp::common::JwtClaims claims;
  std::string err;
  ASSERT_TRUE(chirp::common::JwtVerifyHS256(jwt, "s3cret", &claims, &err)) << err;
  EXPECT_EQ(claims.subject, "user_1");
  // The requested lifetime must materialize as a real exp claim, not be
  // silently dropped.
  EXPECT_GT(claims.expires_at, claims.issued_at);
}

TEST_F(TokenGeneratorTest, UserIdIsDeterministic) {
  const std::string a = TokenGenerator::GenerateUserId("alice");
  const std::string b = TokenGenerator::GenerateUserId("alice");
  const std::string c = TokenGenerator::GenerateUserId("bob");
  EXPECT_TRUE(a.rfind("user_", 0) == 0);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ---------------------------------------------------------------------------
// RateLimiter + BruteForceProtector (against fake Redis)
// ---------------------------------------------------------------------------

class AuthGuardsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    // RedisAuthStore::Connect() probes with GET ping.
    redis_->SetDirect("ping", "pong");
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    RedisAuthStore::Config cfg;
    cfg.port = fake_->port();
    redis_store_ = std::make_shared<RedisAuthStore>(io_, cfg);
    ASSERT_TRUE(redis_store_->Connect());
  }

  void TearDown() override { fake_.reset(); }

  RateLimiter::Config StrictLimits() {
    RateLimiter::Config cfg;
    cfg.max_login_attempts_per_minute = 2;
    cfg.max_login_attempts_per_hour = 3;
    cfg.max_registration_attempts_per_ip_per_hour = 1;
    cfg.max_password_reset_attempts_per_hour = 1;
    cfg.max_api_requests_per_minute = 2;
    cfg.max_api_requests_per_hour = 3;
    return cfg;
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<RedisAuthStore> redis_store_;
};

TEST_F(AuthGuardsTest, LoginLimitBlocksAtMinuteThenHourThresholds) {
  RateLimiter limiter(redis_store_, StrictLimits());

  EXPECT_TRUE(limiter.CheckLoginLimit("alice", "1.1.1.1").allowed);
  EXPECT_TRUE(limiter.CheckLoginLimit("alice", "1.1.1.1").allowed);

  auto minute = limiter.CheckLoginLimit("alice", "1.1.1.1");  // 3rd: minute cap
  EXPECT_FALSE(minute.allowed);
  EXPECT_GT(minute.retry_after_seconds, 0);
  EXPECT_FALSE(minute.error_message.empty());

  // Fresh limiter (separate keys) with a high minute cap but low hour cap.
  RateLimiter::Config cfg = StrictLimits();
  cfg.max_login_attempts_per_minute = 100;
  cfg.max_login_attempts_per_hour = 2;
  RateLimiter hourly(redis_store_, cfg);
  EXPECT_TRUE(hourly.CheckLoginLimit("bob", "2.2.2.2").allowed);
  EXPECT_TRUE(hourly.CheckLoginLimit("bob", "2.2.2.2").allowed);
  EXPECT_FALSE(hourly.CheckLoginLimit("bob", "2.2.2.2").allowed);  // hour cap
}

TEST_F(AuthGuardsTest, RegistrationAndResetAndApiLimits) {
  RateLimiter limiter(redis_store_, StrictLimits());

  EXPECT_TRUE(limiter.CheckRegistrationLimit("3.3.3.3").allowed);
  EXPECT_FALSE(limiter.CheckRegistrationLimit("3.3.3.3").allowed);

  EXPECT_TRUE(limiter.CheckPasswordResetLimit("alice").allowed);
  EXPECT_FALSE(limiter.CheckPasswordResetLimit("alice").allowed);

  EXPECT_TRUE(limiter.CheckApiLimit("u1").allowed);
  EXPECT_TRUE(limiter.CheckApiLimit("u1").allowed);
  EXPECT_FALSE(limiter.CheckApiLimit("u1").allowed);  // minute cap

  RateLimiter::Config cfg = StrictLimits();
  cfg.max_api_requests_per_minute = 100;
  cfg.max_api_requests_per_hour = 2;
  RateLimiter hourly(redis_store_, cfg);
  EXPECT_TRUE(hourly.CheckApiLimit("u2").allowed);
  EXPECT_TRUE(hourly.CheckApiLimit("u2").allowed);
  EXPECT_FALSE(hourly.CheckApiLimit("u2").allowed);  // hour cap
}

TEST_F(AuthGuardsTest, RecordFailureAndResetLimitManipulateCounters) {
  RateLimiter limiter(redis_store_, StrictLimits());
  limiter.RecordFailure("k", "minute");
  limiter.ResetLimit("k", "minute");
  limiter.RecordRequest("k", "minute");  // no-op, must not crash
  EXPECT_TRUE(limiter.GetConfig().max_api_requests_per_minute > 0);

  RateLimiter::Config cfg;
  cfg.max_api_requests_per_minute = 42;
  limiter.SetConfig(cfg);
  EXPECT_EQ(limiter.GetConfig().max_api_requests_per_minute, 42);

  auto created = RateLimiter::Create(redis_store_);
  EXPECT_TRUE(created->CheckLoginLimit("x", "y").allowed);
}

TEST_F(AuthGuardsTest, BruteForceLocksAccountAfterMaxFailures) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector::Config cfg;
  cfg.max_failed_attempts = 3;
  cfg.permanent_lock_threshold = 0;  // disabled
  cfg.lock_by_ip = false;            // IP locking has its own test below
  BruteForceProtector protector(redis_store_, user_store, cfg);

  // Warm up: allowed with no history, plus a warning once 80% reached.
  auto r0 = protector.CheckLoginAttempt("alice", "1.1.1.1");
  EXPECT_TRUE(r0.allowed);
  protector.RecordFailedAttempt("alice", "1.1.1.1");
  protector.RecordFailedAttempt("alice", "1.1.1.1");
  auto warn = protector.CheckLoginAttempt("alice", "1.1.1.1");
  EXPECT_TRUE(warn.allowed);
  EXPECT_FALSE(warn.error_message.empty());  // 2 of 3 -> warning

  protector.RecordFailedAttempt("alice", "1.1.1.1");  // 3 failures
  auto locked = protector.CheckLoginAttempt("alice", "1.1.1.1");
  EXPECT_FALSE(locked.allowed);
  EXPECT_GT(locked.lock_duration_remaining_seconds, 0);

  // Now the account is locked outright.
  auto blocked = protector.CheckLoginAttempt("alice", "1.1.1.1");
  EXPECT_FALSE(blocked.allowed);
  EXPECT_TRUE(blocked.permanently_locked);
  EXPECT_EQ(protector.GetFailedAttemptCount("alice"), 3);

  // Successful login clears counters and unlocks the account.
  protector.RecordSuccess("alice", "1.1.1.1");
  EXPECT_TRUE(protector.CheckLoginAttempt("alice", "1.1.1.1").allowed);
}

TEST_F(AuthGuardsTest, BruteForceIpLockAndPermanentLock) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector::Config cfg;
  cfg.max_failed_attempts = 2;
  cfg.lock_by_ip = true;
  cfg.permanent_lock_threshold = 4;
  BruteForceProtector protector(redis_store_, user_store, cfg);

  // Two failures from the same IP lock the IP for everyone.
  protector.RecordFailedAttempt("alice", "9.9.9.9");
  protector.RecordFailedAttempt("alice", "9.9.9.9");
  protector.RecordFailedAttempt("alice", "9.9.9.9");  // ip count hits threshold
  auto by_ip = protector.CheckLoginAttempt("bob", "9.9.9.9");
  EXPECT_FALSE(by_ip.allowed);
  EXPECT_FALSE(by_ip.permanently_locked);

  // Permanent lock once the per-account threshold is reached.
  protector.RecordFailedAttempt("carol", "8.8.8.8");
  protector.RecordFailedAttempt("carol", "8.8.8.8");
  protector.RecordFailedAttempt("carol", "8.8.8.8");
  protector.RecordFailedAttempt("carol", "8.8.8.8");  // 4 >= permanent threshold
  EXPECT_FALSE(protector.CheckLoginAttempt("carol", "1.1.1.1").allowed);
}

TEST_F(AuthGuardsTest, BruteForceManualLockUnlockAndDurations) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector::Config cfg;
  cfg.max_failed_attempts = 3;
  cfg.base_lock_duration_seconds = 100;
  cfg.max_lock_duration_seconds = 700;
  BruteForceProtector protector(redis_store_, user_store, cfg);

  EXPECT_TRUE(protector.LockAccount("u1", 10));
  EXPECT_TRUE(protector.IsAccountLocked("u1"));
  EXPECT_TRUE(protector.UnlockAccount("u1"));
  EXPECT_FALSE(protector.IsAccountLocked("u1"));

  // Duration defaults to the configured max when 0 is passed.
  EXPECT_TRUE(protector.LockAccount("u1", 0));
  EXPECT_TRUE(protector.IsAccountLocked("u1"));
  EXPECT_TRUE(protector.UnlockAccount("u1"));

  EXPECT_EQ(protector.GetConfig().max_failed_attempts, 3);
  BruteForceProtector::Config cfg2;
  cfg2.max_failed_attempts = 9;
  protector.SetConfig(cfg2);
  EXPECT_EQ(protector.GetConfig().max_failed_attempts, 9);

  auto created = BruteForceProtector::Create(redis_store_, user_store);
  EXPECT_TRUE(created->CheckLoginAttempt("dave", "7.7.7.7").allowed);
}

TEST_F(AuthGuardsTest, LockDurationBackoffIsCapped) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector::Config cfg;
  cfg.max_failed_attempts = 3;
  cfg.base_lock_duration_seconds = 100;
  cfg.max_lock_duration_seconds = 700;
  cfg.lock_by_ip = false;  // keep the IP lock out of the backoff assertions
  BruteForceProtector protector(redis_store_, user_store, cfg);

  // The first Check after exceeding the cap reports the backoff duration and
  // locks the account; later checks take the locked branch, so each scenario
  // starts from a clean slate (RecordSuccess clears counters and unlocks).
  auto scenario = [&](int failures, int64_t expected_seconds) {
    for (int i = 0; i < failures; ++i) {
      protector.RecordFailedAttempt("eve", "1.1.1.1");
    }
    auto r = protector.CheckLoginAttempt("eve", "1.1.1.1");
    EXPECT_FALSE(r.allowed);
    EXPECT_EQ(r.lock_duration_remaining_seconds, expected_seconds);
    protector.RecordSuccess("eve", "1.1.1.1");
  };

  scenario(4, 200);   // excess 2 -> 100 * 2
  scenario(5, 400);   // excess 3 -> 100 * 4
  scenario(7, 700);   // 100 * 16 clipped to the 700s cap
}

TEST_F(AuthGuardsTest, NormalizeIdentifierResolvesUserRows) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector protector(redis_store_, user_store,
                                BruteForceProtector::Config{});

  // Unknown identifier: FindByUsername (no row) then FindByEmail (no row) ->
  // identifier kept as-is.
  fake_mysql::PushRows({});
  fake_mysql::PushRows({});
  EXPECT_EQ(protector.GetFailedAttemptCount("stranger"), 0);

  // Known username resolves to the user_id from the row.
  fake_mysql::PushRows({{"5", "user_9", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  fake_mysql::PushRows({{"5", "user_9", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  protector.RecordSuccess("alice", "1.1.1.1");  // resolves and clears
}

// ---------------------------------------------------------------------------
// AuthService end-to-end over fake MySQL + fake Redis
// ---------------------------------------------------------------------------

class AuthServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    fake_sodium::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    // RedisAuthStore::Connect() probes with GET ping.
    redis_->SetDirect("ping", "pong");
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    AuthService::Config cfg;
    cfg.redis_config.port = fake_->port();
    cfg.rate_limiter_config.max_login_attempts_per_minute = 100;
    cfg.rate_limiter_config.max_login_attempts_per_hour = 100;
    cfg.rate_limiter_config.max_registration_attempts_per_ip_per_hour = 100;
    service_ = std::make_unique<AuthService>(io_, cfg);
  }

  void TearDown() override {
    service_.reset();
    fake_.reset();
  }

  // Scripts the MySQL replies for a successful Initialize().
  void ScriptSuccessfulInitialize() {
    fake_mysql::PushRows({{"users"}});   // users table check
  }

  // Scripts a successful login for `username`: NormalizeIdentifier and
  // VerifyCredentials each fetch the user row, then the session-limit count
  // comes back below the max.
  void ScriptSuccessfulLogin(const std::string& username,
                             const std::string& password,
                             [[maybe_unused]] int max_sessions) {
    const std::string hash = PasswordHasher::HashPassword(password);
    const std::vector<std::optional<std::string>> row = {
        "5", "user_1", username, "a@b.c", hash, "1", "2", "3", "1"};
    fake_mysql::PushRows({row});    // NormalizeIdentifier (CheckLoginAttempt)
    fake_mysql::PushRows({row});    // VerifyCredentials
    fake_mysql::PushRows({row});    // NormalizeIdentifier (RecordSuccess)
    fake_mysql::PushRows({{"1"}});  // active session count < max
  }

  // Captures everything written to fd 2 while alive, so tests can recover
  // values that production code only logs (the raw password-reset token).
  class StderrCapture {
   public:
    StderrCapture() {
      fflush(stderr);
      if (pipe(fds_) != 0) return;
      saved_ = dup(STDERR_FILENO);
      dup2(fds_[1], STDERR_FILENO);
      close(fds_[1]);
      active_ = true;
    }

    ~StderrCapture() { Restore(); }

    std::string Take() {
      Restore();
      std::string out;
      char buf[4096];
      ssize_t n;
      while ((n = read(fds_[0], buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
      }
      return out;
    }

   private:
    void Restore() {
      if (!active_) return;
      active_ = false;
      fflush(stderr);
      dup2(saved_, STDERR_FILENO);
      close(saved_);
    }

    int fds_[2]{-1, -1};
    int saved_{-1};
    bool active_{false};
  };

  // Extracts the raw password-reset token from a captured log blob.
  static std::string TokenFromLog(const std::string& logged) {
    auto pos = logged.find("token: ");
    if (pos == std::string::npos) return "";
    const size_t start = pos + 7;
    const size_t end = logged.find(' ', start);
    return logged.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::unique_ptr<AuthService> service_;
};

TEST_F(AuthServiceTest, InitializeAndShutdownLifecycle) {
  ScriptSuccessfulInitialize();
  EXPECT_TRUE(service_->Initialize());
  service_->Shutdown();
}

TEST_F(AuthServiceTest, InitializeFailsWhenUserStoreCannotInitialize) {
  fake_mysql::SetConnectShouldFail(true);
  EXPECT_FALSE(service_->Initialize());
}

TEST_F(AuthServiceTest, InitializeFailsWhenSessionStoreCannotInitialize) {
  fake_mysql::PushRows({{"users"}});  // user store ok
  fake_mysql::PushRows({{"users"}});  // user store ok (second init attempt)
  fake_mysql::SetConnectShouldFail(true);
  EXPECT_FALSE(service_->Initialize());
}

TEST_F(AuthServiceTest, RegisterHappyPathAndRateDenial) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  fake_mysql::PushRows({{"0"}});  // username free
  fake_mysql::PushRows({{"0"}});  // email free
  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.email = "a@b.c";
  req.password = "Str0ng!pass";
  auto result = service_->Register(req, "1.1.1.1");
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.user_id.empty());

  // Weak passwords are rejected before the DB is touched.
  chirp::auth::UserRegisterRequest weak;
  weak.username = "bob";
  weak.password = "short";
  EXPECT_FALSE(service_->Register(weak, "1.1.1.1").success);
}

TEST_F(AuthServiceTest, RegisterBlockedByRateLimit) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // Burn the registration budget for this IP directly through the store.
  // RedisAuthStore::Connect() probes with GET ping.
  redis_->SetDirect("ping", "pong");
  RedisAuthStore::Config rcfg;
  rcfg.port = fake_->port();
  RedisAuthStore raw(io_, rcfg);
  ASSERT_TRUE(raw.Connect());
  const int budget = 100;
  for (int i = 0; i < budget; ++i) {
    ASSERT_TRUE(raw.CheckRateLimit("register_hour:1.2.3.4", budget));
  }


  chirp::auth::UserRegisterRequest req;
  req.username = "alice";
  req.password = "Str0ng!pass";
  auto result = service_->Register(req, "1.2.3.4");
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::AUTH_FAILED);
}

TEST_F(AuthServiceTest, LoginHappyPathIssuesTokensAndSession) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  ScriptSuccessfulLogin("alice", "Str0ng!pass", 5);
  auto result = service_->Login("alice", "Str0ng!pass", "phone", "ios", "1.1.1.1");
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.user_id, "user_1");
  EXPECT_FALSE(result.session_id.empty());
  EXPECT_FALSE(result.access_token.empty());
  EXPECT_FALSE(result.refresh_token.empty());

  // The access token must validate back to the user.
  EXPECT_EQ(service_->ValidateAccessToken(result.access_token).value_or(""), "user_1");
  // The session resolves through Redis.
  EXPECT_EQ(service_->ValidateSession(result.session_id).value_or(""), "user_1");
}

TEST_F(AuthServiceTest, LoginWithWrongPasswordRecordsFailure) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  auto result = service_->Login("alice", "Wrong!pass1", "phone", "ios", "1.1.1.1");
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::AUTH_FAILED);
}

TEST_F(AuthServiceTest, LoginBlockedByRateLimit) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // RedisAuthStore::Connect() probes with GET ping.
  redis_->SetDirect("ping", "pong");
  RedisAuthStore::Config rcfg;
  rcfg.port = fake_->port();
  RedisAuthStore raw(io_, rcfg);
  ASSERT_TRUE(raw.Connect());
  const int budget = 100;
  for (int i = 0; i < budget; ++i) {
    ASSERT_TRUE(raw.CheckRateLimit("login_minute:1.2.3.4", budget));
  }

  auto result = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.2.3.4");
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::AUTH_FAILED);
}

TEST_F(AuthServiceTest, LoginBlockedWhenAccountLocked) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // RedisAuthStore::Connect() probes with GET ping.
  redis_->SetDirect("ping", "pong");
  RedisAuthStore::Config rcfg;
  rcfg.port = fake_->port();
  RedisAuthStore raw(io_, rcfg);
  ASSERT_TRUE(raw.Connect());
  ASSERT_TRUE(raw.LockAccount("user_1", 60));

  // NormalizeIdentifier resolves "alice" to user_1 via the user row.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  auto result = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.1.1.1");
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("locked") != std::string::npos);
}

TEST_F(AuthServiceTest, LoginFailsWhenSessionLimitReachedWithoutKick) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  const std::vector<std::optional<std::string>> row = {
      "5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"};
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({{"5"}});  // session count == max -> limit reached

  auto result = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.1.1.1");
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::SESSION_EXPIRED);
}

TEST_F(AuthServiceTest, LoginKicksPreviousSessionsWhenConfigured) {
  AuthService::Config cfg;
  cfg.redis_config.port = fake_->port();
  cfg.rate_limiter_config.max_login_attempts_per_minute = 100;
  cfg.rate_limiter_config.max_login_attempts_per_hour = 100;
  cfg.rate_limiter_config.max_registration_attempts_per_ip_per_hour = 100;
  cfg.kick_previous_session = true;
  service_ = std::make_unique<AuthService>(io_, cfg);

  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  const std::vector<std::optional<std::string>> row = {
      "5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"};
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({{"5"}});  // limit reached -> revoke all
  fake_mysql::SetAffectedRows(2);

  auto result = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.1.1.1");
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.kick_previous);
}

TEST_F(AuthServiceTest, LoginFailsWhenSessionCannotBeCreated) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  const std::vector<std::optional<std::string>> row = {
      "5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"};
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({row});
  fake_mysql::PushRows({{"1"}});
  fake_mysql::FailQueriesMatching("INSERT INTO sessions");

  auto result = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.1.1.1");
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_code, chirp::common::INTERNAL_ERROR);
}

TEST_F(AuthServiceTest, RefreshTokenFlow) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // Empty token rejected.
  EXPECT_FALSE(service_->RefreshAccessToken("").success);

  // Unknown token hash rejected.
  EXPECT_FALSE(service_->RefreshAccessToken("rt_bogus").success);

  // Valid refresh token: user row + active session row.
  ScriptSuccessfulLogin("alice", "Str0ng!pass", 5);
  auto login = service_->Login("alice", "Str0ng!pass", "d", "pc", "1.1.1.1");
  ASSERT_TRUE(login.success);

  // Session-store VerifyRefreshToken + GetSession round trip.
  std::vector<std::optional<std::string>> token_row = {
      "1", "tok_1", "user_1", login.session_id, "d", "placeholder", "1",
      "99999999999999", "0", "0"};
  std::vector<std::optional<std::string>> session_row = {
      "1", login.session_id, "user_1", "d", "pc", "1", "99999999999999", "1", "1"};

  // VerifyRefreshToken hashes the raw token and looks it up; scripting the
  // row requires knowing the hash, so use the DB path via hash match: push
  // rows that match whatever hash was searched (single row result).
  fake_mysql::PushRows({token_row});
  fake_mysql::PushRows({session_row});
  auto refreshed = service_->RefreshAccessToken(login.refresh_token);
  EXPECT_TRUE(refreshed.success) << refreshed.error_message;
  EXPECT_EQ(refreshed.user_id, "user_1");
  EXPECT_FALSE(refreshed.access_token.empty());

  // Session inactive -> refresh rejected and token revoked.
  std::vector<std::optional<std::string>> dead_session = {
      "1", login.session_id, "user_1", "d", "pc", "1", "1", "1", "0"};
  fake_mysql::PushRows({token_row});
  fake_mysql::PushRows({dead_session});
  EXPECT_FALSE(service_->RefreshAccessToken(login.refresh_token).success);
}

TEST_F(AuthServiceTest, LogoutAndFriends) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  EXPECT_TRUE(service_->Logout("user_1", "sess_1"));
  EXPECT_EQ(service_->LogoutAll("user_1"), 0);

  auto sessions = service_->GetUserSessions("user_1");
  EXPECT_TRUE(sessions.empty());

  EXPECT_TRUE(service_->RevokeSession("user_1", "sess_2"));

  EXPECT_FALSE(service_->ValidateAccessToken("").has_value());
  EXPECT_FALSE(service_->ValidateAccessToken("not-a-jwt").has_value());

  // ValidateSession falls back to the DB when Redis misses.
  std::vector<std::optional<std::string>> row = {
      "1", "sess_db", "user_db", "d", "pc", "1", "99999999999999", "1", "1"};
  fake_mysql::PushRows({row});
  EXPECT_EQ(service_->ValidateSession("sess_db").value_or(""), "user_db");
  EXPECT_FALSE(service_->ValidateSession("missing").has_value());
}

TEST_F(AuthServiceTest, PasswordResetFlow) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // Unknown identifier: silently "succeeds" without revealing anything.
  fake_mysql::PushRows({});
  fake_mysql::PushRows({});
  EXPECT_TRUE(service_->InitiatePasswordReset("ghost"));

  // Known user: rate check passes, token generated and logged.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  StderrCapture capture;
  ASSERT_TRUE(service_->InitiatePasswordReset("alice"));
  const std::string token = TokenFromLog(capture.Take());
  ASSERT_FALSE(token.empty());

  // Completing with a bogus or empty token fails.
  EXPECT_FALSE(service_->CompletePasswordReset("bogus", "Str0ng!pass2"));
  EXPECT_FALSE(service_->CompletePasswordReset("", "Str0ng!pass2"));

  // Weak new password: the token is consumed and rejected by strength rules.
  EXPECT_FALSE(service_->CompletePasswordReset(token, "short"));

  // Tokens are single-use: the consumed token no longer validates.
  EXPECT_FALSE(service_->CompletePasswordReset(token, "Str0ng!pass2"));
}

TEST_F(AuthServiceTest, PasswordResetConsumesValidTokenAndUpdatesPassword) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  StderrCapture capture;
  ASSERT_TRUE(service_->InitiatePasswordReset("alice"));
  const std::string token = TokenFromLog(capture.Take());
  ASSERT_FALSE(token.empty());

  // Hashing failure aborts the reset.
  fake_sodium::SetPwhashShouldFail(true);
  EXPECT_FALSE(service_->CompletePasswordReset(token, "Str0ng!pass2"));
  fake_sodium::SetPwhashShouldFail(false);

  // Fresh token for the remaining paths.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  StderrCapture capture2;
  ASSERT_TRUE(service_->InitiatePasswordReset("alice"));
  const std::string token2 = TokenFromLog(capture2.Take());
  ASSERT_FALSE(token2.empty());

  // ChangePassword DB failure propagates as failure.
  fake_mysql::PushQueryError("update failed");
  EXPECT_FALSE(service_->CompletePasswordReset(token2, "Str0ng!pass2"));

  // Full success requires a third fresh token.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  StderrCapture capture3;
  ASSERT_TRUE(service_->InitiatePasswordReset("alice"));
  const std::string token3 = TokenFromLog(capture3.Take());
  ASSERT_FALSE(token3.empty());
  EXPECT_TRUE(service_->CompletePasswordReset(token3, "Str0ng!pass3"));
}

TEST_F(AuthServiceTest, ChangePasswordFlow) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // Unknown user.
  fake_mysql::PushRows({});
  EXPECT_FALSE(service_->ChangePassword("user_1", "old", "Str0ng!pass2"));

  // Known user, wrong old password.
  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  EXPECT_FALSE(service_->ChangePassword("user_1", "Wrong!old1", "Str0ng!pass2"));

  // Weak new password.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  EXPECT_FALSE(service_->ChangePassword("user_1", "Str0ng!pass", "short"));

  // Success.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  EXPECT_TRUE(service_->ChangePassword("user_1", "Str0ng!pass", "Str0ng!pass2"));
}

// ---------------------------------------------------------------------------
// Initialize / store-failure propagation
// ---------------------------------------------------------------------------

TEST_F(AuthServiceTest, InitializeFailsWhenUserStoreProbeFails) {
  // UserStore::Initialize probes with SHOW TABLES; failing it must abort
  // the whole service initialization.
  fake_mysql::FailQueriesMatching("SHOW TABLES");
  EXPECT_FALSE(service_->Initialize());
}

TEST_F(AuthServiceTest, InitializeFailsWhenSessionStoreHasNoConnection) {
  // The first connection (UserStore's probe) is granted, every later one
  // is refused -- so SessionStore::Initialize fails after UserStore passed.
  fake_mysql::SetInitShouldFailAfter(1);
  EXPECT_FALSE(service_->Initialize());
}

TEST_F(AuthServiceTest, InitializeContinuesWhenRedisIsDown) {
  AuthService::Config cfg;
  cfg.redis_config.port = 1;  // nothing listens there
  AuthService svc(io_, cfg);
  fake_mysql::PushRows({{"users"}});
  EXPECT_TRUE(svc.Initialize());  // Redis is optional; rate limiting degrades
}

TEST_F(AuthServiceTest, GetUserSessionsMapsStoreRows) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  fake_mysql::PushRows(
      {{"7", "sess_1", "user_1", "dev1", "web", "100", "200", "300", "1"}});
  const auto sessions = service_->GetUserSessions("user_1");
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].session_id, "sess_1");
  EXPECT_EQ(sessions[0].device_id, "dev1");
  EXPECT_EQ(sessions[0].platform, "web");
  EXPECT_EQ(sessions[0].created_at, 100);
  EXPECT_EQ(sessions[0].last_activity_at, 300);
  EXPECT_FALSE(sessions[0].is_current);
}

TEST_F(AuthServiceTest, PasswordResetIsRateLimited) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  // The identifier resolves by username, then the reset-hour counter in
  // Redis is already at the cap.
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});
  redis_->SetDirect("chirp:auth:rate_limit:reset_hour:alice", "999");

  StderrCapture capture;
  EXPECT_FALSE(service_->InitiatePasswordReset("alice"));
  EXPECT_NE(capture.Take().find("rate limit exceeded"), std::string::npos);
}

TEST_F(AuthServiceTest, ChangePasswordFailsWhenHashingFails) {
  ScriptSuccessfulInitialize();
  ASSERT_TRUE(service_->Initialize());

  const std::string hash = PasswordHasher::HashPassword("Str0ng!pass");
  fake_mysql::PushRows({{"5", "user_1", "alice", "a@b.c", hash, "1", "2", "3", "1"}});
  fake_sodium::SetPwhashShouldFail(true);
  EXPECT_FALSE(service_->ChangePassword("user_1", "Str0ng!pass", "Str0ng!pass2"));
}

TEST_F(AuthGuardsTest, GetFailedAttemptCountResolvesEmailToUserId) {
  auto user_store = std::make_shared<chirp::auth::UserStore>(chirp::auth::UserStore::Config{});
  BruteForceProtector protector(redis_store_, user_store, BruteForceProtector::Config{});

  fake_mysql::PushRows({});  // FindByUsername("a@b.c") misses
  fake_mysql::PushRows(
      {{"5", "user_1", "alice", "a@b.c", "h", "1", "2", "3", "1"}});  // FindByEmail hits
  EXPECT_EQ(protector.GetFailedAttemptCount("a@b.c"), 0);
}

}  // namespace
