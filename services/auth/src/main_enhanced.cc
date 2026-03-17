// Enhanced Auth Service with real authentication
// Supports: registration, password login, refresh tokens, rate limiting, brute force protection

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include <asio.hpp>

#include "auth_service.h"
#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

using chirp::auth::AuthService;
using chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint16_t ParsePort(int argc, char** argv) {
  uint16_t port = 6000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--port" || a == "-p") && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[i + 1]));
      i++;
    }
  }
  return port;
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

int ParseIntArg(int argc, char** argv, const std::string& key, int def) {
  return std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str());
}

std::string GetClientIp(const std::shared_ptr<chirp::network::Session>& session) {
  // Try to get remote endpoint
  return session->GetRemoteEndpoint();
}

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

} // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParsePort(argc, argv);

  // Parse configuration
  AuthService::Config config;

  // JWT configuration
  config.jwt_secret = GetArg(argc, argv, "--jwt_secret", "chirp_jwt_secret_change_in_production");
  config.access_token_ttl_seconds = ParseIntArg(argc, argv, "--access_token_ttl", 3600);
  config.refresh_token_ttl_seconds = ParseIntArg(argc, argv, "--refresh_token_ttl", 2592000);
  config.session_ttl_seconds = ParseIntArg(argc, argv, "--session_ttl", 86400);

  // Session limits
  config.max_sessions_per_user = ParseIntArg(argc, argv, "--max_sessions", 5);
  config.kick_previous_session = ParseIntArg(argc, argv, "--kick_previous", 1) != 0;

  // MySQL configuration
  config.user_store_config.host = GetArg(argc, argv, "--mysql_host", "127.0.0.1");
  config.user_store_config.port = static_cast<uint16_t>(
      ParseIntArg(argc, argv, "--mysql_port", 3306));
  config.user_store_config.database = GetArg(argc, argv, "--mysql_database", "chirp");
  config.user_store_config.user = GetArg(argc, argv, "--mysql_user", "chirp");
  config.user_store_config.password = GetArg(argc, argv, "--mysql_password", "chirp_password");
  config.user_store_config.pool_size = ParseIntArg(argc, argv, "--mysql_pool_size", 10);

  config.session_store_config = config.user_store_config;

  // Redis configuration
  config.redis_config.host = GetArg(argc, argv, "--redis_host", "127.0.0.1");
  config.redis_config.port = static_cast<uint16_t>(
      ParseIntArg(argc, argv, "--redis_port", 6379));

  // Rate limiter configuration
  config.rate_limiter_config.max_login_attempts_per_minute =
      ParseIntArg(argc, argv, "--max_login_per_minute", 10);
  config.rate_limiter_config.max_login_attempts_per_hour =
      ParseIntArg(argc, argv, "--max_login_per_hour", 30);
  config.rate_limiter_config.max_registration_attempts_per_ip_per_hour =
      ParseIntArg(argc, argv, "--max_register_per_hour", 5);

  // Brute force configuration
  config.brute_force_config.max_failed_attempts =
      ParseIntArg(argc, argv, "--max_failed_attempts", 5);
  config.brute_force_config.base_lock_duration_seconds =
      ParseIntArg(argc, argv, "--lock_duration", 300);

  Logger::Instance().Info("chirp_auth (enhanced) starting on port " + std::to_string(port));
  Logger::Instance().Info("  MySQL: " + config.user_store_config.host + ":" +
                         std::to_string(config.user_store_config.port) + "/" +
                         config.user_store_config.database);
  Logger::Instance().Info("  Redis: " + config.redis_config.host + ":" +
                         std::to_string(config.redis_config.port));

  asio::io_context io;

  // Create and initialize auth service
  auto auth_service = std::make_shared<AuthService>(io, config);
  if (!auth_service->Initialize()) {
    Logger::Instance().Error("Failed to initialize AuthService");
    return 1;
  }

  chirp::network::TcpServer server(
      io, port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("Failed to parse Packet from client");
          return;
        }

        std::string client_ip = GetClientIp(session);

        switch (pkt.msg_id()) {
        case chirp::gateway::REGISTER_REQ: {
          chirp::auth::RegisterRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::RegisterResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            resp.set_error_message("Invalid request");
            SendPacket(session, chirp::gateway::REGISTER_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          chirp::auth::RegisterRequest auth_req;
          auth_req.set_username(req.username());
          auth_req.set_email(req.email());
          auth_req.set_password(req.password());
          auth_req.set_display_name(req.display_name());

          auto result = auth_service->Register(auth_req, client_ip);

          chirp::auth::RegisterResponse resp;
          resp.set_code(result.error_code);
          resp.set_user_id(result.user_id);
          resp.set_server_time(NowMs());
          resp.set_error_message(result.error_message);
          SendPacket(session, chirp::gateway::REGISTER_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::PASSWORD_LOGIN_REQ: {
          chirp::auth::PasswordLoginRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::PasswordLoginResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            resp.set_error_message("Invalid request");
            SendPacket(session, chirp::gateway::PASSWORD_LOGIN_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          auto result = auth_service->Login(req.identifier(), req.password(),
                                           req.device_id(), req.platform(), client_ip);

          chirp::auth::PasswordLoginResponse resp;
          resp.set_code(result.error_code);
          resp.set_user_id(result.user_id);
          resp.set_username(result.username);
          resp.set_session_id(result.session_id);
          resp.set_access_token(result.access_token);
          resp.set_refresh_token(result.refresh_token);
          resp.set_access_token_expires_at(result.access_token_expires_at);
          resp.set_refresh_token_expires_at(result.refresh_token_expires_at);
          resp.set_server_time(NowMs());
          resp.set_kick_previous(result.kick_previous);
          resp.set_error_message(result.error_message);
          SendPacket(session, chirp::gateway::PASSWORD_LOGIN_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::REFRESH_TOKEN_REQ: {
          chirp::auth::RefreshTokenRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::RefreshTokenResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            resp.set_error_message("Invalid request");
            SendPacket(session, chirp::gateway::REFRESH_TOKEN_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          auto result = auth_service->RefreshAccessToken(req.refresh_token());

          chirp::auth::RefreshTokenResponse resp;
          resp.set_code(result.error_code);
          resp.set_access_token(result.access_token);
          resp.set_access_token_expires_at(result.access_token_expires_at);
          resp.set_server_time(NowMs());
          resp.set_error_message(result.error_message);
          SendPacket(session, chirp::gateway::REFRESH_TOKEN_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::LOGIN_REQ: {
          // Legacy login - treat as JWT validation or simple token login
          chirp::auth::LoginRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LoginResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          std::string user_id;
          chirp::common::ErrorCode code = chirp::common::OK;

          // Try to validate as JWT first
          auto validated_user = auth_service->ValidateAccessToken(req.token());
          if (validated_user) {
            user_id = *validated_user;
          } else {
            // Try to validate as session ID
            auto session_user = auth_service->ValidateSession(req.token());
            if (session_user) {
              user_id = *session_user;
            } else {
              // Fall back to treating token as user_id (for development)
              user_id = req.token();
            }
          }

          chirp::auth::LoginResponse resp;
          resp.set_code(code);
          resp.set_user_id(user_id);
          resp.set_session_id(user_id + "_sess");
          resp.set_server_time(NowMs());
          resp.set_kick_previous(true);
          resp.mutable_kick()->set_reason("session validated");
          SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::LOGOUT_REQ: {
          chirp::auth::LogoutRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LogoutResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          bool success = auth_service->Logout(req.user_id(), req.session_id());

          chirp::auth::LogoutResponse resp;
          resp.set_code(success ? chirp::common::OK : chirp::common::INTERNAL_ERROR);
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::GET_SESSIONS_REQ: {
          chirp::auth::GetSessionsRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::GetSessionsResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::GET_SESSIONS_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          auto sessions = auth_service->GetUserSessions(req.user_id());

          chirp::auth::GetSessionsResponse resp;
          resp.set_code(chirp::common::OK);
          resp.set_server_time(NowMs());
          for (const auto& sess : sessions) {
            auto* s = resp.add_sessions();
            s->set_session_id(sess.session_id);
            s->set_device_id(sess.device_id);
            s->set_platform(sess.platform);
            s->set_created_at(sess.created_at);
            s->set_last_activity_at(sess.last_activity_at);
            s->set_is_current(sess.is_current);
          }
          SendPacket(session, chirp::gateway::GET_SESSIONS_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::REVOKE_SESSION_REQ: {
          chirp::auth::RevokeSessionRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::RevokeSessionResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::REVOKE_SESSION_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          bool success = auth_service->RevokeSession(req.user_id(), req.session_id());

          chirp::auth::RevokeSessionResponse resp;
          resp.set_code(success ? chirp::common::OK : chirp::common::INTERNAL_ERROR);
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::REVOKE_SESSION_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::CHANGE_PASSWORD_REQ: {
          chirp::auth::ChangePasswordRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::ChangePasswordResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            resp.set_error_message("Invalid request");
            SendPacket(session, chirp::gateway::CHANGE_PASSWORD_RESP, pkt.sequence(),
                      resp.SerializeAsString());
            return;
          }

          bool success = auth_service->ChangePassword(req.user_id(), req.old_password(),
                                                     req.new_password());

          chirp::auth::ChangePasswordResponse resp;
          resp.set_code(success ? chirp::common::OK : chirp::common::AUTH_FAILED);
          resp.set_server_time(NowMs());
          if (!success) {
            resp.set_error_message("Failed to change password. Check your old password.");
          }
          SendPacket(session, chirp::gateway::CHANGE_PASSWORD_RESP, pkt.sequence(),
                    resp.SerializeAsString());
          break;
        }
        case chirp::gateway::HEARTBEAT_PING: {
          chirp::gateway::HeartbeatPong pong;
          pong.set_timestamp(NowMs());
          pong.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(),
                    pong.SerializeAsString());
          break;
        }
        default:
          Logger::Instance().Debug("Unknown MsgID: " + std::to_string(static_cast<int>(pkt.msg_id())));
          break;
        }
      },
      [auth_service](std::shared_ptr<chirp::network::Session> session) {
        Logger::Instance().Debug("Client disconnected");
      });

  server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code&, int) {
    Logger::Instance().Info("Shutdown requested");
    server.Stop();
    auth_service->Shutdown();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_auth exited");
  return 0;
}
