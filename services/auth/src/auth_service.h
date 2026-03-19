#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "error_code.h"
#include "brute_force_protector.h"
#include "rate_limiter.h"
#include "redis_auth_store.h"
#include "session_store.h"
#include "user_store.h"

namespace chirp::auth {

/// @brief Main authentication service
/// Orchestrates user registration, login, session management, and token refresh
class AuthService {
public:
  /// @brief Configuration
  struct Config {
    // JWT settings
    std::string jwt_secret = "change_this_secret_in_production";
    int64_t access_token_ttl_seconds = 3600;      // 1 hour
    int64_t refresh_token_ttl_seconds = 2592000;  // 30 days
    int64_t session_ttl_seconds = 86400;          // 24 hours

    // Session limits
    int max_sessions_per_user = 5;
    bool kick_previous_session = false;  // If true, new login kicks old sessions

    // Database config
    UserStore::Config user_store_config;
    SessionStore::Config session_store_config;

    // Redis config
    RedisAuthStore::Config redis_config;

    // Rate limiter config
    RateLimiter::Config rate_limiter_config;

    // Brute force config
    BruteForceProtector::Config brute_force_config;
  };

  /// @brief Login result
  struct LoginResult {
    bool success{false};
    std::string user_id;
    std::string username;
    std::string session_id;
    std::string access_token;
    std::string refresh_token;
    int64_t access_token_expires_at{0};
    int64_t refresh_token_expires_at{0};
    bool kick_previous{false};
    std::string kick_reason;
    chirp::common::ErrorCode error_code{chirp::common::OK};
    std::string error_message;
  };

  /// @brief Refresh token result
  struct RefreshResult {
    bool success{false};
    std::string user_id;
    std::string session_id;
    std::string access_token;
    int64_t access_token_expires_at{0};
    chirp::common::ErrorCode error_code{chirp::common::OK};
    std::string error_message;
  };

  /// @brief Session info
  struct SessionInfo {
    std::string session_id;
    std::string device_id;
    std::string platform;
    int64_t created_at{0};
    int64_t last_activity_at{0};
    bool is_current{false};
  };

  explicit AuthService(asio::io_context& io, const Config& config);
  ~AuthService();

  /// @brief Initialize the service
  bool Initialize();

  /// @brief Shutdown the service
  void Shutdown();

  /// @brief Register a new user
  RegisterResult Register(const RegisterRequest& req, const std::string& ip_address = "");

  /// @brief Login with username/email and password
  LoginResult Login(std::string_view identifier,
                   std::string_view password,
                   const std::string& device_id,
                   const std::string& platform,
                   const std::string& ip_address = "");

  /// @brief Login with refresh token
  RefreshResult RefreshAccessToken(const std::string& refresh_token);

  /// @brief Logout from a session
  bool Logout(const std::string& user_id, const std::string& session_id);

  /// @brief Logout from all sessions
  int LogoutAll(const std::string& user_id);

  /// @brief Get all sessions for a user
  std::vector<SessionInfo> GetUserSessions(const std::string& user_id);

  /// @brief Revoke a specific session
  bool RevokeSession(const std::string& user_id, const std::string& session_id);

  /// @brief Validate access token and get user_id
  std::optional<std::string> ValidateAccessToken(const std::string& token);

  /// @brief Validate session
  std::optional<std::string> ValidateSession(const std::string& session_id);

  /// @brief Initiate password reset
  bool InitiatePasswordReset(const std::string& identifier);

  /// @brief Complete password reset with token
  bool CompletePasswordReset(const std::string& token, std::string_view new_password);

  /// @brief Change password (requires old password)
  bool ChangePassword(const std::string& user_id,
                     std::string_view old_password,
                     std::string_view new_password);

  /// @brief Get configuration
  const Config& GetConfig() const { return config_; }

private:
  std::string GenerateAccessToken(const std::string& user_id, int64_t expires_at);
  std::string GenerateRefreshToken();

  asio::io_context& io_;
  Config config_;

  std::shared_ptr<UserStore> user_store_;
  std::shared_ptr<SessionStore> session_store_;
  std::shared_ptr<RedisAuthStore> redis_store_;
  std::shared_ptr<RateLimiter> rate_limiter_;
  std::shared_ptr<BruteForceProtector> brute_force_protector_;

  // Password reset token storage (token -> user_id + expiry)
  struct PasswordResetToken {
    std::string user_id;
    int64_t expires_at;
  };
  std::unordered_map<std::string, PasswordResetToken> password_reset_tokens_;
  std::mutex password_reset_mutex_;
};

} // namespace chirp::auth
