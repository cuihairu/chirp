#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "proto/auth.pb.h"

namespace chirp::gateway {

class AuthClient;

// Test hook: stops the internal worker and drops the pimpl so the
// destructor takes its empty guard (defined in auth_client.cc). Used by
// internal unit tests.
void DrainAndDropAuthClientForTest(AuthClient& client);

class AuthClient {
public:
  using LoginCallback = std::function<void(const chirp::auth::LoginResponse&)>;
  using LogoutCallback = std::function<void(const chirp::auth::LogoutResponse&)>;

  AuthClient(asio::io_context& main_io, std::string host, uint16_t port);
  ~AuthClient();

  AuthClient(const AuthClient&) = delete;
  AuthClient& operator=(const AuthClient&) = delete;

  void AsyncLogin(const chirp::auth::LoginRequest& req, int64_t seq, LoginCallback cb);
  void AsyncLogout(const chirp::auth::LogoutRequest& req, int64_t seq, LogoutCallback cb);

private:
  // Test access: internal tests befriend this tag to reach private state
  // without changing the compiled token stream.
  friend struct AuthClientInternalAccess;
  friend void DrainAndDropAuthClientForTest(AuthClient& client);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::gateway
