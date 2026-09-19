#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "proto/common.pb.h"

namespace chirp::auth {

/// @brief User data structure
struct UserData {
  int64_t id{0};
  std::string user_id;
  std::string username;
  std::string email;
  std::string password_hash;
  int64_t created_at{0};
  int64_t updated_at{0};
  int64_t last_login_at{0};
  bool is_active{true};
};

/// @brief User registration request
struct UserRegisterRequest {
  std::string username;
  std::string email;
  std::string password;
  std::string display_name;
};

/// @brief User registration result
struct UserRegisterResult {
  bool success{false};
  std::string user_id;
  std::string error_message;
  chirp::common::ErrorCode error_code{chirp::common::OK};
};

/// @brief Backend-neutral user storage interface. The concrete backend
/// (MySQL today, PostgreSQL later) is picked by store_factory.cc; consumers
/// only ever see this header.
class UserStore {
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

  virtual ~UserStore() = default;

  /// @brief Initialize the store and create tables if needed
  virtual bool Initialize() = 0;

  /// @brief Register a new user
  /// @param req Registration request
  /// @return Registration result with user_id or error
  virtual UserRegisterResult Register(const UserRegisterRequest& req) = 0;

  /// @brief Find user by user_id
  virtual std::optional<UserData> FindByUserId(const std::string& user_id) = 0;

  /// @brief Find user by username
  virtual std::optional<UserData> FindByUsername(const std::string& username) = 0;

  /// @brief Find user by email
  virtual std::optional<UserData> FindByEmail(const std::string& email) = 0;

  /// @brief Verify user credentials
  /// @param identifier Username or email
  /// @param password Plain text password
  /// @return User data if credentials valid, nullopt otherwise
  virtual std::optional<UserData> VerifyCredentials(std::string_view identifier,
                                                    std::string_view password) = 0;

  /// @brief Update last login time
  virtual bool UpdateLastLogin(const std::string& user_id, int64_t login_time) = 0;

  /// @brief Change user password
  virtual bool ChangePassword(const std::string& user_id, std::string_view new_password_hash) = 0;

  /// @brief Set user active status
  virtual bool SetActiveStatus(const std::string& user_id, bool is_active) = 0;

  /// @brief Check if username exists
  virtual bool UsernameExists(const std::string& username) = 0;

  /// @brief Check if email exists
  virtual bool EmailExists(const std::string& email) = 0;

  /// @brief Get all active sessions count for a user
  virtual int GetActiveSessionCount(const std::string& user_id) = 0;
};

} // namespace chirp::auth
