#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "error_code.h"

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
struct RegisterRequest {
  std::string username;
  std::string email;
  std::string password;
  std::string display_name;
};

/// @brief User registration result
struct RegisterResult {
  bool success{false};
  std::string user_id;
  std::string error_message;
  chirp::common::ErrorCode error_code{chirp::common::OK};
};

/// @brief MySQL-based user storage interface
class UserStore {
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

  explicit UserStore(const Config& config);
  ~UserStore();

  /// @brief Initialize the store and create tables if needed
  bool Initialize();

  /// @brief Register a new user
  /// @param req Registration request
  /// @return Registration result with user_id or error
  RegisterResult Register(const RegisterRequest& req);

  /// @brief Find user by user_id
  std::optional<UserData> FindByUserId(const std::string& user_id);

  /// @brief Find user by username
  std::optional<UserData> FindByUsername(const std::string& username);

  /// @brief Find user by email
  std::optional<UserData> FindByEmail(const std::string& email);

  /// @brief Verify user credentials
  /// @param identifier Username or email
  /// @param password Plain text password
  /// @return User data if credentials valid, nullopt otherwise
  std::optional<UserData> VerifyCredentials(std::string_view identifier,
                                            std::string_view password);

  /// @brief Update last login time
  bool UpdateLastLogin(const std::string& user_id, int64_t login_time);

  /// @brief Change user password
  bool ChangePassword(const std::string& user_id, std::string_view new_password_hash);

  /// @brief Set user active status
  bool SetActiveStatus(const std::string& user_id, bool is_active);

  /// @brief Check if username exists
  bool UsernameExists(const std::string& username);

  /// @brief Check if email exists
  bool EmailExists(const std::string& email);

  /// @brief Get all active sessions count for a user
  int GetActiveSessionCount(const std::string& user_id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::auth
