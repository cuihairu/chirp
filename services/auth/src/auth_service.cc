#include "auth_service.h"

#include "jwt.h"
#include "logger.h"
#include "password_hasher.h"
#include "token_generator.h"

using chirp::common::Logger;

namespace chirp::auth {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t NowSeconds() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

AuthService::AuthService(asio::io_context& io, const Config& config)
    : io_(io), config_(config) {

  user_store_ = std::make_shared<UserStore>(config_.user_store_config);
  session_store_ = std::make_shared<SessionStore>(config_.session_store_config);
  redis_store_ = std::make_shared<RedisAuthStore>(io_, config_.redis_config);
  rate_limiter_ = std::make_shared<RateLimiter>(redis_store_, config_.rate_limiter_config);
  brute_force_protector_ = std::make_shared<BruteForceProtector>(
      redis_store_, user_store_, config_.brute_force_config);
}

AuthService::~AuthService() {
  Shutdown();
}

bool AuthService::Initialize() {
  Logger::Instance().Info("Initializing AuthService...");

  if (!user_store_->Initialize()) {
    Logger::Instance().Error("Failed to initialize UserStore");
    return false;
  }

  if (!session_store_->Initialize()) {
    Logger::Instance().Error("Failed to initialize SessionStore");
    return false;
  }

  if (!redis_store_->Connect()) {
    Logger::Instance().Warn("Failed to connect to Redis, rate limiting disabled");
  }

  Logger::Instance().Info("AuthService initialized successfully");
  return true;
}

void AuthService::Shutdown() {
  Logger::Instance().Info("Shutting down AuthService...");
  redis_store_->Disconnect();
}

RegisterResult AuthService::Register(const RegisterRequest& req, const std::string& ip_address) {
  Logger::Instance().Info("Registration request for: " + req.username);

  // Check rate limit
  auto rate_check = rate_limiter_->CheckRegistrationLimit(ip_address);
  if (!rate_check.allowed) {
    RegisterResult result;
    result.success = false;
    result.error_message = rate_check.error_message;
    result.error_code = chirp::common::RATE_LIMITED;
    return result;
  }

  // Attempt registration
  RegisterResult result = user_store_->Register(req);

  if (result.success) {
    Logger::Instance().Info("User registered successfully: " + result.user_id);
  } else {
    Logger::Instance().Warn("Registration failed: " + result.error_message);
  }

  return result;
}

LoginResult AuthService::Login(std::string_view identifier,
                               std::string_view password,
                               const std::string& device_id,
                               const std::string& platform,
                               const std::string& ip_address) {
  LoginResult result;
  std::string identifier_str(identifier);

  Logger::Instance().Info("Login request for: " + identifier_str + " from " + ip_address);

  // Check rate limit
  auto rate_check = rate_limiter_->CheckLoginLimit(identifier_str, ip_address);
  if (!rate_check.allowed) {
    result.success = false;
    result.error_code = chirp::common::RATE_LIMITED;
    result.error_message = rate_check.error_message;
    return result;
  }

  // Check brute force protection
  auto brute_check = brute_force_protector_->CheckLoginAttempt(identifier_str, ip_address);
  if (!brute_check.allowed) {
    result.success = false;
    result.error_code = chirp::common::ACCOUNT_LOCKED;
    result.error_message = brute_check.error_message;
    return result;
  }

  // Verify credentials
  auto user_data = user_store_->VerifyCredentials(identifier, password);
  if (!user_data) {
    // Record failed attempt
    brute_force_protector_->RecordFailedAttempt(identifier_str, ip_address);

    result.success = false;
    result.error_code = chirp::common::AUTH_FAILED;
    result.error_message = "Invalid credentials";
    Logger::Instance().Warn("Failed login attempt for: " + identifier_str);
    return result;
  }

  // Record successful login
  brute_force_protector_->RecordSuccess(identifier_str, ip_address);

  result.user_id = user_data->user_id;
  result.username = user_data->username;

  // Check session limit
  if (!session_store_->CheckSessionLimit(user_data->user_id, config_.max_sessions_per_user)) {
    // Need to revoke oldest session or all previous sessions
    if (config_.kick_previous_session) {
      int revoked = session_store_->RevokeAllUserSessions(user_data->user_id);
      result.kick_previous = true;
      result.kick_reason = "New login from another device";
      Logger::Instance().Info("Revoked " + std::to_string(revoked) + " sessions for user: " +
                             user_data->user_id);
    } else {
      result.success = false;
      result.error_code = chirp::common::SESSION_LIMIT_REACHED;
      result.error_message = "Maximum session limit reached. Please logout from other devices.";
      return result;
    }
  }

  // Create session
  CreateSessionRequest session_req;
  session_req.user_id = user_data->user_id;
  session_req.device_id = device_id;
  session_req.platform = platform;
  session_req.ttl_seconds = static_cast<int>(config_.session_ttl_seconds);

  auto session_data = session_store_->CreateSession(session_req);
  if (!session_data) {
    result.success = false;
    result.error_code = chirp::common::INTERNAL_ERROR;
    result.error_message = "Failed to create session";
    return result;
  }

  result.session_id = session_data->session_id;

  // Generate access token
  int64_t access_expires_at = NowSeconds() + config_.access_token_ttl_seconds;
  result.access_token = GenerateAccessToken(user_data->user_id, access_expires_at);
  result.access_token_expires_at = access_expires_at * 1000;  // Convert to ms

  // Generate refresh token
  std::string refresh_token = GenerateRefreshToken();
  std::string refresh_token_hash = TokenGenerator::HashToken(refresh_token);

  CreateRefreshTokenRequest refresh_req;
  refresh_req.user_id = user_data->user_id;
  refresh_req.session_id = session_data->session_id;
  refresh_req.device_id = device_id;
  refresh_req.ttl_seconds = static_cast<int>(config_.refresh_token_ttl_seconds);

  auto refresh_data = session_store_->CreateRefreshToken(refresh_req, refresh_token_hash);
  if (refresh_data) {
    result.refresh_token = refresh_token;
    result.refresh_token_expires_at = refresh_data->expires_at;
  }

  // Store session in Redis for distributed access
  redis_store_->StoreSession(session_data->session_id,
                           user_data->user_id,
                           device_id,
                           platform,
                           session_data->expires_at);

  result.success = true;
  result.error_code = chirp::common::OK;

  Logger::Instance().Info("Login successful for user: " + user_data->user_id +
                         " session: " + session_data->session_id);

  return result;
}

RefreshResult AuthService::RefreshAccessToken(const std::string& refresh_token) {
  RefreshResult result;

  if (refresh_token.empty()) {
    result.success = false;
    result.error_code = chirp::common::INVALID_PARAM;
    result.error_message = "Refresh token is required";
    return result;
  }

  // Hash the token and look it up
  std::string token_hash = TokenGenerator::HashToken(refresh_token);
  auto refresh_data = session_store_->VerifyRefreshToken(token_hash);

  if (!refresh_data) {
    result.success = false;
    result.error_code = chirp::common::AUTH_FAILED;
    result.error_message = "Invalid or expired refresh token";
    return result;
  }

  // Check if the session is still valid
  auto session_data = session_store_->GetSession(refresh_data->session_id);
  if (!session_data || !session_data->is_active) {
    // Revoke the refresh token if session is invalid
    session_store_->RevokeRefreshToken(refresh_data->token_id);

    result.success = false;
    result.error_code = chirp::common::SESSION_EXPIRED;
    result.error_message = "Session expired or invalid";
    return result;
  }

  // Generate new access token
  int64_t access_expires_at = NowSeconds() + config_.access_token_ttl_seconds;
  result.access_token = GenerateAccessToken(refresh_data->user_id, access_expires_at);
  result.access_token_expires_at = access_expires_at * 1000;

  result.user_id = refresh_data->user_id;
  result.session_id = refresh_data->session_id;
  result.success = true;
  result.error_code = chirp::common::OK;

  Logger::Instance().Info("Token refreshed for user: " + refresh_data->user_id);

  return result;
}

bool AuthService::Logout(const std::string& user_id, const std::string& session_id) {
  Logger::Instance().Info("Logout request for user: " + user_id + " session: " + session_id);

  // Revoke session in database
  bool db_result = session_store_->RevokeSession(session_id);

  // Remove from Redis
  bool redis_result = redis_store_->DeleteSession(session_id);

  // Revoke associated refresh tokens
  session_store_->RevokeSessionRefreshTokens(session_id);

  return db_result || redis_result;
}

int AuthService::LogoutAll(const std::string& user_id) {
  Logger::Instance().Info("Logout all sessions for user: " + user_id);

  // Get all sessions
  auto sessions = session_store_->GetUserSessions(user_id);

  // Revoke all sessions
  int db_count = session_store_->RevokeAllUserSessions(user_id);

  // Revoke all refresh tokens
  session_store_->RevokeAllUserRefreshTokens(user_id);

  // Remove from Redis
  int redis_count = redis_store_->DeleteAllUserSessions(user_id);

  return db_count + redis_count;
}

std::vector<AuthService::SessionInfo> AuthService::GetUserSessions(const std::string& user_id) {
  auto sessions = session_store_->GetUserSessions(user_id);

  std::vector<SessionInfo> result;
  result.reserve(sessions.size());

  for (const auto& session : sessions) {
    SessionInfo info;
    info.session_id = session.session_id;
    info.device_id = session.device_id;
    info.platform = session.platform;
    info.created_at = session.created_at;
    info.last_activity_at = session.last_activity_at;
    info.is_current = false;
    result.push_back(std::move(info));
  }

  return result;
}

bool AuthService::RevokeSession(const std::string& user_id, const std::string& session_id) {
  return Logout(user_id, session_id);
}

std::optional<std::string> AuthService::ValidateAccessToken(const std::string& token) {
  if (token.empty()) {
    return std::nullopt;
  }

  chirp::common::JwtClaims claims;
  std::string err;

  if (!chirp::common::JwtVerifyHS256(token, config_.jwt_secret, &claims, &err)) {
    Logger::Instance().Debug("JWT validation failed: " + err);
    return std::nullopt;
  }

  // Note: Our current JWT implementation doesn't support expiration
  // In production, add exp claim support

  return claims.subject;
}

std::optional<std::string> AuthService::ValidateSession(const std::string& session_id) {
  // Check Redis first for distributed access
  auto user_id = redis_store_->GetSessionUser(session_id);
  if (user_id) {
    return user_id;
  }

  // Fallback to database
  auto session_data = session_store_->GetSession(session_id);
  if (session_data && session_data->is_active && session_data->expires_at > NowMs()) {
    return session_data->user_id;
  }

  return std::nullopt;
}

bool AuthService::InitiatePasswordReset(const std::string& identifier) {
  // Find user by username or email
  auto user_data = user_store_->FindByUsername(identifier);
  if (!user_data) {
    user_data = user_store_->FindByEmail(identifier);
  }

  if (!user_data) {
    // Don't reveal that user doesn't exist
    return true;
  }

  // Check rate limit
  auto rate_check = rate_limiter_->CheckPasswordResetLimit(identifier);
  if (!rate_check.allowed) {
    Logger::Instance().Warn("Password reset rate limit exceeded for: " + identifier);
    return false;
  }

  // TODO: Generate and store reset token, send email
  // For now, just log
  Logger::Instance().Info("Password reset initiated for: " + user_data->user_id);

  return true;
}

bool AuthService::CompletePasswordReset(const std::string& token, std::string_view new_password) {
  // TODO: Verify reset token and update password
  Logger::Instance().Info("Password reset completion requested");
  return false;
}

bool AuthService::ChangePassword(const std::string& user_id,
                                std::string_view old_password,
                                std::string_view new_password) {
  auto user_data = user_store_->FindByUserId(user_id);
  if (!user_data) {
    return false;
  }

  // Verify old password
  if (!PasswordHasher::VerifyPassword(old_password, user_data->password_hash)) {
    return false;
  }

  // Validate new password strength
  std::string validation_error = PasswordHasher::ValidateStrength(new_password);
  if (!validation_error.empty()) {
    Logger::Instance().Warn("Password change failed validation: " + validation_error);
    return false;
  }

  // Hash new password
  std::string new_hash = PasswordHasher::HashPassword(new_password);
  if (new_hash.empty()) {
    return false;
  }

  // Update password
  return user_store_->ChangePassword(user_id, new_hash);
}

std::string AuthService::GenerateAccessToken(const std::string& user_id, int64_t expires_at) {
  return TokenGenerator::GenerateAccessToken(user_id, config_.jwt_secret,
                                            expires_at - NowSeconds());
}

std::string AuthService::GenerateRefreshToken() {
  return TokenGenerator::GenerateRefreshToken();
}

} // namespace chirp::auth
