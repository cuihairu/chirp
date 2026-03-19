#include "rate_limiter.h"

#include "logger.h"

namespace chirp::auth {
namespace {

std::string KeyFor(const std::string& prefix, const std::string& identifier) {
  return prefix + ":" + identifier;
}

} // namespace

RateLimiter::RateLimiter(std::shared_ptr<RedisAuthStore> redis_store, const Config& config)
    : redis_store_(std::move(redis_store)), config_(config) {}

RateLimiter::~RateLimiter() = default;

RateLimiter::Result RateLimiter::CheckLoginLimit(const std::string& identifier,
                                                 const std::string& ip_address) {
  Result result;
  result.limit = config_.max_login_attempts_per_minute;

  // Check minute window
  std::string minute_key = KeyFor("login_minute", ip_address);
  if (!redis_store_->CheckRateLimit(minute_key, config_.max_login_attempts_per_minute)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(minute_key);
    result.retry_after_seconds = config_.minute_window_seconds;
    result.error_message = "Too many login attempts. Please try again later.";
    Logger::Instance().Warn("Rate limit exceeded for login from IP: " + ip_address);
    return result;
  }

  // Check hour window
  std::string hour_key = KeyFor("login_hour", ip_address);
  if (!redis_store_->CheckRateLimit(hour_key, config_.max_login_attempts_per_hour)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(hour_key);
    result.retry_after_seconds = config_.hour_window_seconds;
    result.error_message = "Too many login attempts. Please wait before trying again.";
    Logger::Instance().Warn("Hourly rate limit exceeded for login from IP: " + ip_address);
    return result;
  }

  result.allowed = true;
  return result;
}

RateLimiter::Result RateLimiter::CheckRegistrationLimit(const std::string& ip_address) {
  Result result;
  result.limit = config_.max_registration_attempts_per_ip_per_hour;

  std::string key = KeyFor("register_hour", ip_address);
  if (!redis_store_->CheckRateLimit(key, config_.max_registration_attempts_per_ip_per_hour)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(key);
    result.retry_after_seconds = config_.hour_window_seconds;
    result.error_message = "Too many registration attempts. Please try again later.";
    Logger::Instance().Warn("Registration rate limit exceeded for IP: " + ip_address);
    return result;
  }

  result.allowed = true;
  return result;
}

RateLimiter::Result RateLimiter::CheckPasswordResetLimit(const std::string& identifier) {
  Result result;
  result.limit = config_.max_password_reset_attempts_per_hour;

  std::string key = KeyFor("reset_hour", identifier);
  if (!redis_store_->CheckRateLimit(key, config_.max_password_reset_attempts_per_hour)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(key);
    result.retry_after_seconds = config_.hour_window_seconds;
    result.error_message = "Too many password reset attempts. Please try again later.";
    return result;
  }

  result.allowed = true;
  return result;
}

RateLimiter::Result RateLimiter::CheckApiLimit(const std::string& user_id_or_ip) {
  Result result;
  result.limit = config_.max_api_requests_per_minute;

  std::string minute_key = KeyFor("api_minute", user_id_or_ip);
  if (!redis_store_->CheckRateLimit(minute_key, config_.max_api_requests_per_minute)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(minute_key);
    result.retry_after_seconds = config_.minute_window_seconds;
    result.error_message = "API rate limit exceeded.";
    return result;
  }

  // Also check hourly limit
  std::string hour_key = KeyFor("api_hour", user_id_or_ip);
  if (!redis_store_->CheckRateLimit(hour_key, config_.max_api_requests_per_hour)) {
    result.allowed = false;
    result.current_count = redis_store_->GetRateLimitCount(hour_key);
    result.retry_after_seconds = config_.hour_window_seconds;
    result.error_message = "Hourly API limit exceeded.";
    return result;
  }

  result.allowed = true;
  return result;
}

void RateLimiter::RecordRequest(const std::string& key, const std::string& window) {
  // RateLimiter uses CheckRateLimit which auto-increments
  // This is a no-op for the Redis-based implementation
}

void RateLimiter::RecordFailure(const std::string& key, const std::string& window) {
  // Increment failure counter separately
  redis_store_->CheckRateLimit(KeyFor(key, window), 1000000);  // Effectively counts
}

void RateLimiter::ResetLimit(const std::string& key, const std::string& window) {
  redis_store_->ResetRateLimit(KeyFor(key, window));
}

} // namespace chirp::auth
