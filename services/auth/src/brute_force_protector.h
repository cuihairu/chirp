#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

namespace chirp::auth {

class RedisAuthStore;
class UserStore;

/// @brief Brute force protection for authentication
/// Tracks failed attempts and implements progressive delays
class BruteForceProtector {
public:
  /// @brief Configuration
  struct Config {
    int max_failed_attempts = 5;           // Lock after N attempts
    int base_lock_duration_seconds = 300;   // 5 minutes base
    int max_lock_duration_seconds = 86400;  // 24 hours max
    int attempt_window_seconds = 900;       // 15 minutes tracking window
    bool lock_by_ip = true;                 // Also lock by IP address
    bool permanent_lock_threshold = 20;     // Permanent lock after N attempts (0 = disabled)
  };

  /// @brief Protection check result
  struct Result {
    bool allowed{true};
    int failed_attempts{0};
    int64_t lock_duration_remaining_seconds{0};
    bool permanently_locked{false};
    std::string error_message;
  };

  explicit BruteForceProtector(std::shared_ptr<RedisAuthStore> redis_store,
                              std::shared_ptr<UserStore> user_store,
                              const Config& config = Config{});
  ~BruteForceProtector();

  /// @brief Check if login attempt should be allowed
  Result CheckLoginAttempt(const std::string& identifier, const std::string& ip_address);

  /// @brief Record a failed login attempt
  void RecordFailedAttempt(const std::string& identifier, const std::string& ip_address);

  /// @brief Record a successful login (clears failed attempts)
  void RecordSuccess(const std::string& identifier, const std::string& ip_address);

  /// @brief Manually lock an account
  bool LockAccount(const std::string& user_id, int64_t duration_seconds = 0);

  /// @brief Unlock an account
  bool UnlockAccount(const std::string& user_id);

  /// @brief Check if account is locked
  bool IsAccountLocked(const std::string& user_id);

  /// @brief Get current failed attempt count
  int GetFailedAttemptCount(const std::string& identifier);

  /// @brief Get configuration
  const Config& GetConfig() const { return config_; }

  /// @brief Update configuration
  void SetConfig(const Config& config) { config_ = config; }

private:
  int64_t CalculateLockDuration(int failed_attempts);
  std::string NormalizeIdentifier(const std::string& identifier);

  std::shared_ptr<RedisAuthStore> redis_store_;
  std::shared_ptr<UserStore> user_store_;
  Config config_;
};

} // namespace chirp::auth
