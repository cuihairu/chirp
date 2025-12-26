#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "proto/auth.pb.h"

namespace chirp::gateway {

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace chirp::gateway
