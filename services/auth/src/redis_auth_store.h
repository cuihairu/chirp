#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <asio.hpp>

namespace chirp::auth {

/// @brief Redis-backed distributed session store
/// Provides fast, distributed session management for multi-instance deployments
class RedisAuthStore {
public:
  /// @brief Configuration
  struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    int session_ttl_seconds = 3600;      // 1 hour default
    int refresh_token_ttl_seconds = 2592000;  // 30 days default
    int rate_limit_ttl_seconds = 60;     // 1 minute window
  };

  explicit RedisAuthStore(asio::io_context& io, const Config& config);
  ~RedisAuthStore();

  /// @brief Connect to Redis
  bool Connect();

  /// @brief Disconnect from Redis
  void Disconnect();

  /// @brief Check connection status
  bool IsConnected() const;

  // Session operations
  /// @brief Store session data in Redis
  bool StoreSession(const std::string& session_id,
                   const std::string& user_id,
                   const std::string& device_id,
                   const std::string& platform,
                   int64_t expires_at);

  /// @brief Get session data from Redis
  std::optional<std::string> GetSessionUser(const std::string& session_id);

  /// @brief Update session activity
  bool UpdateSessionActivity(const std::string& session_id, int64_t activity_time);

  /// @brief Delete session from Redis
  bool DeleteSession(const std::string& session_id);

  /// @brief Delete all sessions for a user
  int DeleteAllUserSessions(const std::string& user_id);

  // Refresh token operations
  /// @brief Store refresh token mapping
  bool StoreRefreshToken(const std::string& token_id,
                        const std::string& user_id,
                        const std::string& session_id,
                        int64_t expires_at);

  /// @brief Get refresh token data
  std::optional<std::string> GetRefreshTokenUser(const std::string& token_id);

  /// @brief Delete refresh token
  bool DeleteRefreshToken(const std::string& token_id);

  // Rate limiting operations
  /// @brief Check and increment rate limit counter
  /// @param key Rate limit key (e.g., "login_attempt:IP_ADDRESS")
  /// @param max_attempts Maximum attempts allowed
  /// @return true if under limit, false if limit exceeded
  bool CheckRateLimit(const std::string& key, int max_attempts);

  /// @brief Get current rate limit count
  int GetRateLimitCount(const std::string& key);

  /// @brief Reset rate limit for a key
  bool ResetRateLimit(const std::string& key);

  // Brute force protection
  /// @brief Record failed login attempt
  bool RecordFailedLogin(const std::string& identifier, const std::string& ip_address);

  /// @brief Get failed login attempt count
  int GetFailedLoginCount(const std::string& identifier);

  /// @brief Clear failed login attempts (e.g., after successful login)
  bool ClearFailedLogins(const std::string& identifier);

  // Lock operations for account security
  /// @brief Check if account is locked
  bool IsAccountLocked(const std::string& user_id);

  /// @brief Lock account for specified duration
  bool LockAccount(const std::string& user_id, int64_t lock_duration_seconds);

  /// @brief Unlock account
  bool UnlockAccount(const std::string& user_id);

  // Device tracking
  /// @brief Get all devices for a user
  std::vector<std::string> GetUserDevices(const std::string& user_id);

  /// @brief Remove device from user
  bool RemoveDevice(const std::string& user_id, const std::string& device_id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::auth
