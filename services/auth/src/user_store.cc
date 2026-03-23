#include "user_store.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <random>

#include <mysql/mysql.h>

#include "logger.h"
#include "password_hasher.h"
#include "token_generator.h"

namespace chirp::auth {
namespace {

using chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Helper to escape strings for MySQL queries
std::string EscapeString(MYSQL* mysql, const std::string& str) {
  std::vector<char> escaped(str.size() * 2 + 1);
  mysql_real_escape_string(mysql, escaped.data(), str.c_str(), str.size());
  return std::string(escaped.data());
}

} // namespace

struct UserStore::Impl {
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

    // Reuse existing connection if available
    if (!connections.empty()) {
      auto* conn = connections.back();
      connections.pop_back();
      if (mysql_ping(conn) == 0) {
        return conn;
      }
      mysql_close(conn);
    }

    // Create new connection
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
      return nullptr;
    }

    const bool reconnect = true;
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

UserStore::UserStore(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

UserStore::~UserStore() = default;

bool UserStore::Initialize() {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    Logger::Instance().Error("Failed to get MySQL connection for UserStore initialization");
    return false;
  }

  // Users table is created by init_db.sql, but verify it exists
  const char* check_table = "SHOW TABLES LIKE 'users'";
  if (mysql_query(conn, check_table) != 0) {
    Logger::Instance().Error("Failed to check users table: " + std::string(mysql_error(conn)));
    impl_->ReturnConnection(conn);
    return false;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  bool has_table = (result && mysql_num_rows(result) > 0);
  if (result) {
    mysql_free_result(result);
  }

  if (!has_table) {
    Logger::Instance().Warn("Users table does not exist, please run init_db.sql");
  }

  impl_->ReturnConnection(conn);
  return true;
}

UserRegisterResult UserStore::Register(const UserRegisterRequest& req) {
  UserRegisterResult result;

  // Validate password strength
  std::string validation_error = PasswordHasher::ValidateStrength(req.password);
  if (!validation_error.empty()) {
    result.success = false;
    result.error_message = validation_error;
    result.error_code = chirp::common::INVALID_PARAM;
    return result;
  }

  // Check if username already exists
  if (UsernameExists(req.username)) {
    result.success = false;
    result.error_message = "Username already exists";
    result.error_code = chirp::common::INVALID_PARAM;
    return result;
  }

  // Check if email already exists
  if (!req.email.empty() && EmailExists(req.email)) {
    result.success = false;
    result.error_message = "Email already exists";
    result.error_code = chirp::common::INVALID_PARAM;
    return result;
  }

  // Hash password
  std::string password_hash = PasswordHasher::HashPassword(req.password);
  if (password_hash.empty()) {
    result.success = false;
    result.error_message = "Failed to hash password";
    result.error_code = chirp::common::INTERNAL_ERROR;
    return result;
  }

  // Generate user_id
  std::string user_id = TokenGenerator::GenerateUserId(req.username);
  int64_t now = NowMs();

  auto* conn = impl_->GetConnection();
  if (!conn) {
    result.success = false;
    result.error_message = "Database connection failed";
    result.error_code = chirp::common::INTERNAL_ERROR;
    return result;
  }

  std::string query = "INSERT INTO users (user_id, username, email, password_hash, "
                      "created_at, updated_at, is_active) VALUES ('" +
                      EscapeString(conn, user_id) + "', '" +
                      EscapeString(conn, req.username) + "', '" +
                      EscapeString(conn, req.email) + "', '" +
                      EscapeString(conn, password_hash) + "', " +
                      std::to_string(now) + ", " +
                      std::to_string(now) + ", 1)";

  if (mysql_query(conn, query.c_str()) != 0) {
    result.success = false;
    result.error_message = "Failed to create user: " + std::string(mysql_error(conn));
    result.error_code = chirp::common::INTERNAL_ERROR;
    impl_->ReturnConnection(conn);
    return result;
  }

  result.success = true;
  result.user_id = user_id;
  result.error_code = chirp::common::OK;

  impl_->ReturnConnection(conn);
  return result;
}

std::optional<UserData> UserStore::FindByUserId(const std::string& user_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string query = "SELECT id, user_id, username, email, password_hash, "
                      "created_at, updated_at, last_login_at, is_active "
                      "FROM users WHERE user_id = '" +
                      EscapeString(conn, user_id) + "'";

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
  std::optional<UserData> data;

  if (row) {
    UserData user;
    user.id = std::stoll(row[0] ? row[0] : "0");
    user.user_id = row[1] ? row[1] : "";
    user.username = row[2] ? row[2] : "";
    user.email = row[3] ? row[3] : "";
    user.password_hash = row[4] ? row[4] : "";
    user.created_at = std::stoll(row[5] ? row[5] : "0");
    user.updated_at = std::stoll(row[6] ? row[6] : "0");
    user.last_login_at = std::stoll(row[7] ? row[7] : "0");
    user.is_active = row[8] ? (std::string(row[8]) == "1") : true;
    data = std::move(user);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

std::optional<UserData> UserStore::FindByUsername(const std::string& username) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string query = "SELECT id, user_id, username, email, password_hash, "
                      "created_at, updated_at, last_login_at, is_active "
                      "FROM users WHERE username = '" +
                      EscapeString(conn, username) + "'";

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
  std::optional<UserData> data;

  if (row) {
    UserData user;
    user.id = std::stoll(row[0] ? row[0] : "0");
    user.user_id = row[1] ? row[1] : "";
    user.username = row[2] ? row[2] : "";
    user.email = row[3] ? row[3] : "";
    user.password_hash = row[4] ? row[4] : "";
    user.created_at = std::stoll(row[5] ? row[5] : "0");
    user.updated_at = std::stoll(row[6] ? row[6] : "0");
    user.last_login_at = std::stoll(row[7] ? row[7] : "0");
    user.is_active = row[8] ? (std::string(row[8]) == "1") : true;
    data = std::move(user);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

std::optional<UserData> UserStore::FindByEmail(const std::string& email) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return std::nullopt;
  }

  std::string query = "SELECT id, user_id, username, email, password_hash, "
                      "created_at, updated_at, last_login_at, is_active "
                      "FROM users WHERE email = '" +
                      EscapeString(conn, email) + "'";

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
  std::optional<UserData> data;

  if (row) {
    UserData user;
    user.id = std::stoll(row[0] ? row[0] : "0");
    user.user_id = row[1] ? row[1] : "";
    user.username = row[2] ? row[2] : "";
    user.email = row[3] ? row[3] : "";
    user.password_hash = row[4] ? row[4] : "";
    user.created_at = std::stoll(row[5] ? row[5] : "0");
    user.updated_at = std::stoll(row[6] ? row[6] : "0");
    user.last_login_at = std::stoll(row[7] ? row[7] : "0");
    user.is_active = row[8] ? (std::string(row[8]) == "1") : true;
    data = std::move(user);
  }

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return data;
}

std::optional<UserData> UserStore::VerifyCredentials(std::string_view identifier,
                                                      std::string_view password) {
  // Try username first, then email
  auto user_data = FindByUsername(std::string(identifier));
  if (!user_data) {
    user_data = FindByEmail(std::string(identifier));
  }

  if (!user_data) {
    return std::nullopt;
  }

  if (!user_data->is_active) {
    Logger::Instance().Warn("User account is inactive: " + user_data->user_id);
    return std::nullopt;
  }

  if (!PasswordHasher::VerifyPassword(password, user_data->password_hash)) {
    return std::nullopt;
  }

  // Update last login
  UpdateLastLogin(user_data->user_id, NowMs());

  return user_data;
}

bool UserStore::UpdateLastLogin(const std::string& user_id, int64_t login_time) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "UPDATE users SET last_login_at = " +
                      std::to_string(login_time) +
                      ", updated_at = " + std::to_string(login_time) +
                      " WHERE user_id = '" + EscapeString(conn, user_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

bool UserStore::ChangePassword(const std::string& user_id, std::string_view new_password_hash) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE users SET password_hash = '" +
                      EscapeString(conn, std::string(new_password_hash)) +
                      "', updated_at = " + std::to_string(now) +
                      " WHERE user_id = '" + EscapeString(conn, user_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

bool UserStore::SetActiveStatus(const std::string& user_id, bool is_active) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  int64_t now = NowMs();
  std::string query = "UPDATE users SET is_active = " + std::string(is_active ? "1" : "0") +
                      ", updated_at = " + std::to_string(now) +
                      " WHERE user_id = '" + EscapeString(conn, user_id) + "'";

  bool result = (mysql_query(conn, query.c_str()) == 0);
  impl_->ReturnConnection(conn);
  return result;
}

bool UserStore::UsernameExists(const std::string& username) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "SELECT COUNT(*) FROM users WHERE username = '" +
                      EscapeString(conn, username) + "'";

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
  bool exists = row && row[0] && std::stoi(row[0]) > 0;

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return exists;
}

bool UserStore::EmailExists(const std::string& email) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "SELECT COUNT(*) FROM users WHERE email = '" +
                      EscapeString(conn, email) + "'";

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
  bool exists = row && row[0] && std::stoi(row[0]) > 0;

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return exists;
}

int UserStore::GetActiveSessionCount(const std::string& user_id) {
  auto* conn = impl_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "SELECT COUNT(*) FROM sessions WHERE user_id = '" +
                      EscapeString(conn, user_id) +
                      "' AND is_active = 1 AND expires_at > " + std::to_string(NowMs());

  if (mysql_query(conn, query.c_str()) != 0) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    impl_->ReturnConnection(conn);
    return 0;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  int count = row && row[0] ? std::stoi(row[0]) : 0;

  mysql_free_result(result);
  impl_->ReturnConnection(conn);
  return count;
}

} // namespace chirp::auth
