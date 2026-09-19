#pragma once

#include <memory>

#include "session_store.h"

namespace chirp::auth {

/// @brief MySQL-backed SessionStore implementation (libmariadb/libmysqlclient
/// C API behind a pimpl; SQL and connection pooling live in the .cc).
class MySQLSessionStore final : public SessionStore {
public:
  explicit MySQLSessionStore(const Config& config);
  ~MySQLSessionStore() override;

  bool Initialize() override;
  std::optional<SessionData> CreateSession(const CreateSessionRequest& req) override;
  std::optional<SessionData> GetSession(const std::string& session_id) override;
  std::vector<SessionData> GetUserSessions(const std::string& user_id) override;
  bool UpdateSessionActivity(const std::string& session_id, int64_t activity_time) override;
  bool RevokeSession(const std::string& session_id) override;
  int RevokeOtherSessions(const std::string& user_id,
                          const std::string& keep_session_id) override;
  int RevokeAllUserSessions(const std::string& user_id) override;
  int CleanupExpiredSessions() override;
  std::optional<RefreshTokenData> CreateRefreshToken(const CreateRefreshTokenRequest& req,
                                                     const std::string& token_hash) override;
  std::optional<RefreshTokenData> GetRefreshToken(const std::string& token_id) override;
  std::optional<RefreshTokenData> VerifyRefreshToken(const std::string& token_hash) override;
  bool RevokeRefreshToken(const std::string& token_id) override;
  int RevokeAllUserRefreshTokens(const std::string& user_id) override;
  int RevokeSessionRefreshTokens(const std::string& session_id) override;
  int CleanupExpiredRefreshTokens() override;
  bool CheckSessionLimit(const std::string& user_id, int max_sessions) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::auth
