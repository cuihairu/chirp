#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <asio.hpp>

namespace chirp::auth {

class RedisAuthStore;

/// @brief Rate limiter for API endpoints and auth operations
/// Implements token bucket and sliding window algorithms
class RateLimiter {
public:
  /// @brief Rate limit configuration
  struct Config {
    // Login attempts
    int max_login_attempts_per_minute = 10;
    int max_login_attempts_per_hour = 30;

    // Registration
    int max_registration_attempts_per_ip_per_hour = 5;

    // Password reset
    int max_password_reset_attempts_per_hour = 3;

    // API requests (general)
    int max_api_requests_per_minute = 60;
    int max_api_requests_per_hour = 1000;

    // Default window TTL
    int minute_window_seconds = 60;
    int hour_window_seconds = 3600;
  };

  /// @brief Rate limit check result
  struct Result {
    bool allowed{true};
    int current_count{0};
    int limit{0};
    int64_t retry_after_seconds{0};
    std::string error_message;
  };

  // Constructor without default parameter to avoid self-reference
  explicit RateLimiter(std::shared_ptr<RedisAuthStore> redis_store,
                      const Config& config);
  ~RateLimiter();

  /// @brief Check login rate limit for identifier
  Result CheckLoginLimit(const std::string& identifier, const std::string& ip_address);

  /// @brief Check registration rate limit for IP
  Result CheckRegistrationLimit(const std::string& ip_address);

  /// @brief Check password reset rate limit
  Result CheckPasswordResetLimit(const std::string& identifier);

  /// @brief Check general API rate limit
  Result CheckApiLimit(const std::string& user_id_or_ip);

  /// @brief Record a successful request (for token bucket)
  void RecordRequest(const std::string& key, const std::string& window);

  /// @brief Record a failed attempt
  void RecordFailure(const std::string& key, const std::string& window);

  /// @brief Reset rate limit for a key
  void ResetLimit(const std::string& key, const std::string& window);

  /// @brief Get configuration
  const Config& GetConfig() const { return config_; }

  /// @brief Update configuration
  void SetConfig(const Config& config) { config_ = config; }

  /// @brief Create with default config
  static std::shared_ptr<RateLimiter> Create(std::shared_ptr<RedisAuthStore> redis_store);

private:
  std::shared_ptr<RedisAuthStore> redis_store_;
  Config config_;
};

inline RateLimiter::Config MakeDefaultRateLimiterConfig() {
  return RateLimiter::Config{};
}

} // namespace chirp::auth
