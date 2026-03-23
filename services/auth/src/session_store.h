#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chirp::auth {

/// @brief Session data structure
struct SessionData {
  int64_t id{0};
  std::string session_id;
  std::string user_id;
  std::string device_id;
  std::string platform;  // "ios", "android", "web", "pc"
  int64_t created_at{0};
  int64_t expires_at{0};
  int64_t last_activity_at{0};
  bool is_active{true};
};

/// @brief Refresh token data structure
struct RefreshTokenData {
  int64_t id{0};
  std::string token_id;
  std::string user_id;
  std::string session_id;
  std::string device_id;
  std::string token_hash;
  int64_t created_at{0};
  int64_t expires_at{0};
  int64_t revoked_at{0};
  bool is_revoked{false};
};

/// @brief Create session request
struct CreateSessionRequest {
  std::string user_id;
  std::string device_id;
  std::string platform;
  int64_t ttl_seconds;  // Session TTL
};

/// @brief Refresh token creation request
struct CreateRefreshTokenRequest {
  std::string user_id;
  std::string session_id;
  std::string device_id;
  int64_t ttl_seconds;  // Refresh token TTL (typically longer than session)
};

/// @brief MySQL-based session and refresh token storage
class SessionStore {
public:
  /// @brief Connection configuration
  struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string database = "chirp";
    std::string user = "chirp";
    std::string password = "chirp_password";
    size_t pool_size = 10;
  };

  explicit SessionStore(const Config& config);
  ~SessionStore();

  /// @brief Initialize the store
  bool Initialize();

  /// @brief Create a new session
  std::optional<SessionData> CreateSession(const CreateSessionRequest& req);

  /// @brief Get session by session_id
  std::optional<SessionData> GetSession(const std::string& session_id);

  /// @brief Get all active sessions for a user
  std::vector<SessionData> GetUserSessions(const std::string& user_id);

  /// @brief Update session activity timestamp
  bool UpdateSessionActivity(const std::string& session_id, int64_t activity_time);

  /// @brief Revoke a session
  bool RevokeSession(const std::string& session_id);

  /// @brief Revoke all sessions for a user except one
  int RevokeOtherSessions(const std::string& user_id, const std::string& keep_session_id);

  /// @brief Revoke all sessions for a user
  int RevokeAllUserSessions(const std::string& user_id);

  /// @brief Clean up expired sessions
  int CleanupExpiredSessions();

  /// @brief Create a refresh token
  std::optional<RefreshTokenData> CreateRefreshToken(const CreateRefreshTokenRequest& req,
                                                     const std::string& token_hash);

  /// @brief Get refresh token by token_id
  std::optional<RefreshTokenData> GetRefreshToken(const std::string& token_id);

  /// @brief Verify refresh token by hash
  std::optional<RefreshTokenData> VerifyRefreshToken(const std::string& token_hash);

  /// @brief Revoke a refresh token
  bool RevokeRefreshToken(const std::string& token_id);

  /// @brief Revoke all refresh tokens for a user
  int RevokeAllUserRefreshTokens(const std::string& user_id);

  /// @brief Revoke all refresh tokens for a session
  int RevokeSessionRefreshTokens(const std::string& session_id);

  /// @brief Clean up expired refresh tokens
  int CleanupExpiredRefreshTokens();

  /// @brief Check session rate limit (max sessions per user)
  bool CheckSessionLimit(const std::string& user_id, int max_sessions);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::auth
