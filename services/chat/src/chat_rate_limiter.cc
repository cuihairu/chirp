#include "chat_rate_limiter.h"

#include "logger.h"

using Logger = chirp::common::Logger;

namespace chirp::chat {
namespace {

constexpr const char* kLoginKeyPrefix = "chirp:chat:rl:login:";
constexpr const char* kSendKeyPrefix = "chirp:chat:rl:send:";

}  // namespace

ChatRateLimiter::ChatRateLimiter(std::shared_ptr<chirp::network::RedisClient> redis,
                                 Config config)
    : redis_(std::move(redis)), config_(config) {}

ChatRateLimiter::Result ChatRateLimiter::CheckLogin(const std::string& client_ip) {
  const std::string identity = client_ip.empty() ? "unknown" : client_ip;
  return Check(std::string(kLoginKeyPrefix) + identity,
               config_.max_logins_per_minute_per_ip);
}

ChatRateLimiter::Result ChatRateLimiter::CheckSend(const std::string& user_id) {
  const std::string identity = user_id.empty() ? "unknown" : user_id;
  return Check(std::string(kSendKeyPrefix) + identity,
               config_.max_sends_per_minute_per_user);
}

ChatRateLimiter::Result ChatRateLimiter::Check(const std::string& key, int limit) {
  Result result;
  result.limit = limit;

  // Disabled (no Redis) or misconfigured (limit <= 0): always allow.
  if (!redis_ || limit <= 0) {
    return result;
  }

  // Counters are server-owned, so the stored value is always numeric in
  // practice; on any transport failure both Get and SetEx fail and we fail
  // open below, mirroring RedisAuthStore::CheckRateLimit.
  const auto current = redis_->Get(key);
  const int count = current ? std::stoi(*current) : 0;
  if (count >= limit) {
    Logger::Instance().Warn("chat rate limit exceeded for " + key + " (" +
                            std::to_string(count) + "/" + std::to_string(limit) + ")");
    result.allowed = false;
    result.current_count = count;
    return result;
  }

  if (!redis_->SetEx(key, std::to_string(count + 1), config_.window_seconds)) {
    // Could not record the hit: allow rather than lock clients out.
    return result;
  }
  result.current_count = count + 1;
  return result;
}

}  // namespace chirp::chat
