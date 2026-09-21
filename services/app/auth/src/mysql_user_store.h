#pragma once

#include <memory>

#include "user_store.h"

namespace chirp::auth {

/// @brief MySQL-backed UserStore implementation (libmariadb/libmysqlclient
/// C API behind a pimpl; SQL and connection pooling live in the .cc).
class MySQLUserStore final : public UserStore {
public:
  explicit MySQLUserStore(const Config& config);
  ~MySQLUserStore() override;

  bool Initialize() override;
  UserRegisterResult Register(const UserRegisterRequest& req) override;
  std::optional<UserData> FindByUserId(const std::string& user_id) override;
  std::optional<UserData> FindByUsername(const std::string& username) override;
  std::optional<UserData> FindByEmail(const std::string& email) override;
  std::optional<UserData> VerifyCredentials(std::string_view identifier,
                                            std::string_view password) override;
  bool UpdateLastLogin(const std::string& user_id, int64_t login_time) override;
  bool ChangePassword(const std::string& user_id, std::string_view new_password_hash) override;
  bool SetActiveStatus(const std::string& user_id, bool is_active) override;
  bool UsernameExists(const std::string& username) override;
  bool EmailExists(const std::string& email) override;
  int GetActiveSessionCount(const std::string& user_id) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::auth
