#pragma once

#include <cstdint>
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

/// @brief Backend-neutral session and refresh token storage interface. The
/// concrete backend (MySQL today, PostgreSQL later) is picked by
/// store_factory.cc; consumers only ever see this header.
class SessionStore {
public:
  /// @brief Connection configuration (backend-neutral: host/port/database/
  /// user/password/pool_size hold for every SQL backend)
  struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string database = "chirp";
    std::string user = "chirp";
    std::string password = "chirp_password";
    size_t pool_size = 10;
  };

  virtual ~SessionStore() = default;

  /// @brief Initialize the store
  virtual bool Initialize() = 0;

  /// @brief Create a new session
  virtual std::optional<SessionData> CreateSession(const CreateSessionRequest& req) = 0;

  /// @brief Get session by session_id
  virtual std::optional<SessionData> GetSession(const std::string& session_id) = 0;

  /// @brief Get all active sessions for a user
  virtual std::vector<SessionData> GetUserSessions(const std::string& user_id) = 0;

  /// @brief Update session activity timestamp
  virtual bool UpdateSessionActivity(const std::string& session_id, int64_t activity_time) = 0;

  /// @brief Revoke a session
  virtual bool RevokeSession(const std::string& session_id) = 0;

  /// @brief Revoke all sessions for a user except one
  virtual int RevokeOtherSessions(const std::string& user_id,
                                  const std::string& keep_session_id) = 0;

  /// @brief Revoke all sessions for a user
  virtual int RevokeAllUserSessions(const std::string& user_id) = 0;

  /// @brief Clean up expired sessions
  virtual int CleanupExpiredSessions() = 0;

  /// @brief Create a refresh token
  virtual std::optional<RefreshTokenData> CreateRefreshToken(
      const CreateRefreshTokenRequest& req, const std::string& token_hash) = 0;

  /// @brief Get refresh token by token_id
  virtual std::optional<RefreshTokenData> GetRefreshToken(const std::string& token_id) = 0;

  /// @brief Verify refresh token by hash
  virtual std::optional<RefreshTokenData> VerifyRefreshToken(const std::string& token_hash) = 0;

  /// @brief Revoke a refresh token
  virtual bool RevokeRefreshToken(const std::string& token_id) = 0;

  /// @brief Revoke all refresh tokens for a user
  virtual int RevokeAllUserRefreshTokens(const std::string& user_id) = 0;

  /// @brief Revoke all refresh tokens for a session
  virtual int RevokeSessionRefreshTokens(const std::string& session_id) = 0;

  /// @brief Clean up expired refresh tokens
  virtual int CleanupExpiredRefreshTokens() = 0;

  /// @brief Check session rate limit (max sessions per user)
  virtual bool CheckSessionLimit(const std::string& user_id, int max_sessions) = 0;
};

} // namespace chirp::auth
