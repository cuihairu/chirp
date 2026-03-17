#include "session_store.h"

#include <cstring>
#include <mutex>

#include <mysql.h>

#include "common/logger.h"
#include "token_generator.h"

namespace chirp::auth {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string EscapeString(MYSQL* mysql, const std::string& str) {
  std::vector<char> escaped(str.size() * 2 + 1);
  mysql_real_escape_string(mysql, escaped.data(), str.c_str(), str.size());
  return std::string(escaped.data());
}

} // namespace

struct SessionStore::Impl {
  Config config;
  std::mutex mutex;
  std::vector<MYSQL*> connections;

  Impl(const Config& cfg) : config(cfg) {}

  ~Impl() {
    for (auto* conn : connections) {
      mysql_close(conn);
    }
  }

  MYSQL* GetConnection() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!connections.empty()) {
      auto* conn = connections.back();
      connections.pop_back();
      if (mysql_ping(conn) == 0) {
        return conn;
      }
      mysql_close(conn);
    }

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
      return nullptr;
    }

    my_bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn,
                            config.host.c_str(),
                            config.user.c_str(),
                            config.password.c_str(),
                            config.database.c_str(),
                            config.port,
                            nullptr,
                            CLIENT_MULTI_STATEMENTS)) {
      Logger::Instance().Error("MySQL connection failed: " + std::string(mysql_error(conn)));
      mysql_close(conn);
      return nullptr;
    }

    return conn;
  }

  void ReturnConnection(MYSQL* conn) {
    if (conn && connections.size() < config.pool_size) {
      std::lock_guard<std::mutex> lock(mutex);
      connections.push_back(conn);
    } else if (conn) {
      mysql_close(conn);
    }
  }
};

SessionStore::SessionStore(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

SessionStore::~SessionStore() = default;

bool SessionStore::Initialize() {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  // Tables are created by init_db.sql
  impl_->ReturnConnection(conn);
  return true;
}

std::optional<SessionData> SessionStore::CreateSession(const CreateSessionRequest& req) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string session_id = TokenGenerator::GenerateSessionId(req.user_id);
  int64_t now = NowMs();
  int64_t expires_at = now + (req.ttl_seconds * 1000);

  std::string query = "INSERT INTO sessions (session_id, user_id, device_id, platform, "
                      "created_at, expires_at, last_activity_at, is_active) VALUES ('" +
                      EscapeString(conn, session_id) + "', '" +
                      EscapeString(conn, req.user_id) + "', '" +
                      EscapeString(conn, req.device_id) + "', '" +
                      EscapeString(conn, req.platform) + "', " +
                      std::to_string(now) + ", " +
                      std::to_string(expires_at) + ", " +
                      std::to_string(now) + ", 1)";

  if (mysql_query(conn, query.c_str()) != 0) {
    Logger::Instance().Error("Failed to create session: " + std::string(mysql_error(conn)));
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  SessionData session;
  session.session_id = session_id;
  session.user_id = req.user_id;
  session.device_id = req.device_id;
  session.platform = req.platform;
  session.created_at = now;
  session.expires_at = expires_at;
  session.last_activity_at = now;
  session.is_active = true;

  impl_->ReturnConnection(conn);
  return session;
}

std::optional<SessionData> SessionStore::GetSession(const std::string& session_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string query = "SELECT id, session_id, user_id, device_id, platform, "
                      "created_at, expires_at, last_activity_at, is_active "
                      "FROM sessions WHERE session_id = '" +
                      EscapeString(conn, session_id) + "'";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  std::optional<SessionData> data;

  if (row) {
    SessionData session;
    session.id = std::stoll(row[0] ? row[0] : "0");
    session.session_id = row[1] ? row[1] : "";
    session.user_id = row[2] ? row[2] : "";
    session.device_id = row[3] ? row[3] : "";
    session.platform = row[4] ? row[4] : "";
    session.created_at = std::stoll(row[5] ? row[5] : "0");
    session.expires_at = std::stoll(row[6] ? row[6] : "0");
    session.last_activity_at = std::stoll(row[7] ? row[7] : "0");
    session.is_active = row[8] ? (std::string(row[8]) == "1") : true;
    data = std::move(session);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

std::vector<SessionData> SessionStore::GetUserSessions(const std::string& user_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return {};
  }

  std::string query = "SELECT id, session_id, user_id, device_id, platform, "
                      "created_at, expires_at, last_activity_at, is_active "
                      "FROM sessions WHERE user_id = '" +
                      EscapeString(conn, user_id) +
                      "' AND is_active = 1 AND expires_at > " + std::to_string(NowMs()) +
                      " ORDER BY created_at DESC";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return {};
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return {};
  }

  std::vector<SessionData> sessions;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result))) {
    SessionData session;
    session.id = std::stoll(row[0] ? row[0] : "0");
    session.session_id = row[1] ? row[1] : "";
    session.user_id = row[2] ? row[2] : "";
    session.device_id = row[3] ? row[3] : "";
    session.platform = row[4] ? row[4] : "";
    session.created_at = std::stoll(row[5] ? row[5] : "0");
    session.expires_at = std::stoll(row[6] ? row[6] : "0");
    session.last_activity_at = std::stoll(row[7] ? row[7] : "0");
    session.is_active = row[8] ? (std::string(row[8]) == "1") : true;
    sessions.push_back(std::move(session));
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return sessions;
}

bool SessionStore::UpdateSessionActivity(const std::string& session_id, int64_t activity_time) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "UPDATE sessions SET last_activity_at = " +
                      std::to_string(activity_time) +
                      " WHERE session_id = '" + EscapeString(conn, session_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

bool SessionStore::RevokeSession(const std::string& session_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE sessions SET is_active = 0 WHERE session_id = '" +
                      EscapeString(conn, session_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

int SessionStore::RevokeOtherSessions(const std::string& user_id, const std::string& keep_session_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "UPDATE sessions SET is_active = 0 WHERE user_id = '" +
                      EscapeString(conn, user_id) +
                      "' AND session_id != '" + EscapeString(conn, keep_session_id) +
                      "' AND is_active = 1";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

int SessionStore::RevokeAllUserSessions(const std::string& user_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "UPDATE sessions SET is_active = 0 WHERE user_id = '" +
                      EscapeString(conn, user_id) + "'";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

int SessionStore::CleanupExpiredSessions() {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "DELETE FROM sessions WHERE expires_at < " +
                      std::to_string(NowMs()) + " OR is_active = 0";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

std::optional<RefreshTokenData> SessionStore::CreateRefreshToken(const CreateRefreshTokenRequest& req,
                                                                 const std::string& token_hash) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string token_id = TokenGenerator::GenerateTokenId();
  int64_t now = NowMs();
  int64_t expires_at = now + (req.ttl_seconds * 1000);

  std::string query = "INSERT INTO refresh_tokens (token_id, user_id, session_id, device_id, "
                      "token_hash, created_at, expires_at) VALUES ('" +
                      EscapeString(conn, token_id) + "', '" +
                      EscapeString(conn, req.user_id) + "', '" +
                      EscapeString(conn, req.session_id) + "', '" +
                      EscapeString(conn, req.device_id) + "', '" +
                      EscapeString(conn, token_hash) + "', " +
                      std::to_string(now) + ", " +
                      std::to_string(expires_at) + ")";

  if (mysql_query(conn, query.c_str()) != 0) {
    Logger::Instance().Error("Failed to create refresh token: " + std::string(mysql_error(conn)));
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  RefreshTokenData token;
  token.token_id = token_id;
  token.user_id = req.user_id;
  token.session_id = req.session_id;
  token.device_id = req.device_id;
  token.token_hash = token_hash;
  token.created_at = now;
  token.expires_at = expires_at;
  token.is_revoked = false;

  impl_->ReturnConnection(conn);
  return token;
}

std::optional<RefreshTokenData> SessionStore::GetRefreshToken(const std::string& token_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string query = "SELECT id, token_id, user_id, session_id, device_id, token_hash, "
                      "created_at, expires_at, revoked_at, is_revoked "
                      "FROM refresh_tokens WHERE token_id = '" +
                      EscapeString(conn, token_id) + "'";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  std::optional<RefreshTokenData> data;

  if (row) {
    RefreshTokenData token;
    token.id = std::stoll(row[0] ? row[0] : "0");
    token.token_id = row[1] ? row[1] : "";
    token.user_id = row[2] ? row[2] : "";
    token.session_id = row[3] ? row[3] : "";
    token.device_id = row[4] ? row[4] : "";
    token.token_hash = row[5] ? row[5] : "";
    token.created_at = std::stoll(row[6] ? row[6] : "0");
    token.expires_at = std::stoll(row[7] ? row[7] : "0");
    token.revoked_at = std::stoll(row[8] ? row[8] : "0");
    token.is_revoked = row[9] ? (std::string(row[9]) == "1") : false;
    data = std::move(token);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

std::optional<RefreshTokenData> SessionStore::VerifyRefreshToken(const std::string& token_hash) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  int64_t now = NowMs();
  std::string query = "SELECT id, token_id, user_id, session_id, device_id, token_hash, "
                      "created_at, expires_at, revoked_at, is_revoked "
                      "FROM refresh_tokens WHERE token_hash = '" +
                      EscapeString(conn, token_hash) +
                      "' AND is_revoked = 0 AND expires_at > " + std::to_string(now);

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return std::nullopt;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  std::optional<RefreshTokenData> data;

  if (row) {
    RefreshTokenData token;
    token.id = std::stoll(row[0] ? row[0] : "0");
    token.token_id = row[1] ? row[1] : "";
    token.user_id = row[2] ? row[2] : "";
    token.session_id = row[3] ? row[3] : "";
    token.device_id = row[4] ? row[4] : "";
    token.token_hash = row[5] ? row[5] : "";
    token.created_at = std::stoll(row[6] ? row[6] : "0");
    token.expires_at = std::stoll(row[7] ? row[7] : "0");
    token.revoked_at = std::stoll(row[8] ? row[8] : "0");
    token.is_revoked = row[9] ? (std::string(row[9]) == "1") : false;
    data = std::move(token);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

bool SessionStore::RevokeRefreshToken(const std::string& token_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE refresh_tokens SET is_revoked = 1, revoked_at = " +
                      std::to_string(now) +
                      " WHERE token_id = '" + EscapeString(conn, token_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

int SessionStore::RevokeAllUserRefreshTokens(const std::string& user_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE refresh_tokens SET is_revoked = 1, revoked_at = " +
                      std::to_string(now) +
                      " WHERE user_id = '" + EscapeString(conn, user_id) + "'";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

int SessionStore::RevokeSessionRefreshTokens(const std::string& session_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE refresh_tokens SET is_revoked = 1, revoked_at = " +
                      std::to_string(now) +
                      " WHERE session_id = '" + EscapeString(conn, session_id) + "'";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

int SessionStore::CleanupExpiredRefreshTokens() {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "DELETE FROM refresh_tokens WHERE expires_at < " +
                      std::to_string(NowMs()) + " OR is_revoked = 1";

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  int affected = static_cast<int>(mysql_affected_rows(conn));
  impl_->ReturnConnection(conn);
  return affected;
}

bool SessionStore::CheckSessionLimit(const std::string& user_id, int max_sessions) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "SELECT COUNT(*) FROM sessions WHERE user_id = '" +
                      EscapeString(conn, user_id) +
                      "' AND is_active = 1 AND expires_at > " + std::to_string(NowMs());

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return false;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return false;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  int count = row && row[0] ? std::stoi(row[0]) : 0;

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return count < max_sessions;
}

} // namespace chirp::auth
