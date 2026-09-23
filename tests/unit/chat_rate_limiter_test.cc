// ChatRateLimiter: fixed-window abuse gates for the chat direct entry.
// Drives the real RedisClient through the loopback FakeRedisServer, so the
// transport failure paths (server error reply, dead port) are exercised
// end-to-end.

#include "chat_rate_limiter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fake_servers.h"
#include "in_memory_redis.h"

namespace {

using chirp::chat::ChatRateLimiter;

ChatRateLimiter::Config StrictConfig() {
  ChatRateLimiter::Config cfg;
  cfg.max_logins_per_minute_per_ip = 2;
  cfg.max_sends_per_minute_per_user = 3;
  return cfg;
}

class ChatRateLimiterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });
    client_ = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake_->port());
  }

  void TearDown() override {
    client_.reset();
    fake_.reset();
  }

  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<chirp::network::RedisClient> client_;
};

TEST_F(ChatRateLimiterTest, AllowsUpToLimitThenDenies) {
  ChatRateLimiter limiter(client_, StrictConfig());

  EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);
  EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);

  const auto third = limiter.CheckLogin("10.0.0.1");
  EXPECT_FALSE(third.allowed);
  EXPECT_EQ(third.current_count, 2);
  EXPECT_EQ(third.limit, 2);
}

TEST_F(ChatRateLimiterTest, SeparateAddressesAndKindsDoNotShareBudget) {
  ChatRateLimiter limiter(client_, StrictConfig());

  EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);
  EXPECT_TRUE(limiter.CheckLogin("10.0.0.2").allowed);
  EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);

  const auto send = limiter.CheckSend("alice");
  EXPECT_TRUE(send.allowed);
  EXPECT_EQ(send.current_count, 1);
  EXPECT_EQ(send.limit, 3);
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:send:alice"), "1");
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:login:10.0.0.1"), "2");
}

TEST_F(ChatRateLimiterTest, EmptyIdentityFallsUnderUnknownKey) {
  ChatRateLimiter limiter(client_, StrictConfig());

  EXPECT_TRUE(limiter.CheckLogin("").allowed);
  EXPECT_TRUE(limiter.CheckSend("").allowed);
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:login:unknown"), "1");
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:send:unknown"), "1");
}

TEST_F(ChatRateLimiterTest, WithoutRedisFailsOpen) {
  ChatRateLimiter disabled(nullptr, StrictConfig());

  for (int i = 0; i < 10; ++i) {
    const auto gate = disabled.CheckLogin("10.0.0.1");
    EXPECT_TRUE(gate.allowed);
    EXPECT_EQ(gate.current_count, 0);
  }
}

TEST_F(ChatRateLimiterTest, DeadRedisServerFailsOpen) {
  // Nothing listens on this port: connect fails for every command.
  ChatRateLimiter limiter(
      std::make_shared<chirp::network::RedisClient>("127.0.0.1", static_cast<uint16_t>(1)),
      StrictConfig());

  EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);
  EXPECT_TRUE(limiter.CheckSend("alice").allowed);
}

TEST_F(ChatRateLimiterTest, RedisReadFailureFailsOpen) {
  fake_.reset();  // Drop the in-memory handler...
  redis_.reset();
  chirp_test::FakeRedisServer broken([](const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "GET") {
      return std::string("-ERR simulated outage\r\n");
    }
    return chirp_test::Simple("OK");
  });
  ChatRateLimiter limiter(
      std::make_shared<chirp::network::RedisClient>("127.0.0.1", broken.port()),
      StrictConfig());

  // Every read fails, so the counter never reaches the limit: fail open.
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);
  }
}

TEST_F(ChatRateLimiterTest, RedisWriteFailureStillAllowsTheCurrentHit) {
  redis_.reset();
  fake_.reset();
  chirp_test::FakeRedisServer broken([](const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "SET") {
      return std::string("-ERR simulated outage\r\n");
    }
    return std::string("$-1\r\n");  // GET: key missing (null bulk)
  });
  ChatRateLimiter limiter(
      std::make_shared<chirp::network::RedisClient>("127.0.0.1", broken.port()),
      StrictConfig());

  // Get misses (null bulk) but SetEx fails: the hit is allowed and nothing
  // accumulates, so the client never locks out while Redis is degraded.
  for (int i = 0; i < 5; ++i) {
    const auto gate = limiter.CheckSend("alice");
    EXPECT_TRUE(gate.allowed);
    EXPECT_EQ(gate.current_count, 0);
  }
}

TEST_F(ChatRateLimiterTest, NonPositiveLimitDisablesTheGate) {
  ChatRateLimiter::Config cfg;
  cfg.max_logins_per_minute_per_ip = 0;
  cfg.max_sends_per_minute_per_user = 0;
  ChatRateLimiter limiter(client_, cfg);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(limiter.CheckLogin("10.0.0.1").allowed);
    EXPECT_TRUE(limiter.CheckSend("alice").allowed);
  }
  EXPECT_TRUE(redis_->GetDirect("chirp:chat:rl:login:10.0.0.1").empty());
}

TEST_F(ChatRateLimiterTest, HeapAllocatedIdentityBuildsTheSameKey) {
  ChatRateLimiter limiter(client_, StrictConfig());

  // Past the SSO buffer so the concatenated redis key takes the heap path.
  const std::string long_ip(80, '7');
  EXPECT_TRUE(limiter.CheckLogin(long_ip).allowed);
  EXPECT_TRUE(limiter.CheckSend(long_ip).allowed);
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:login:" + long_ip), "1");
  EXPECT_EQ(redis_->GetDirect("chirp:chat:rl:send:" + long_ip), "1");
}

}  // namespace
