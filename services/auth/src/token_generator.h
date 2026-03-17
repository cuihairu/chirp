#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chirp::auth {

/// @brief Secure token generation for sessions, refresh tokens, and reset tokens
class TokenGenerator {
public:
  /// @brief Generate a cryptographically random token ID
  /// @param bytes Number of random bytes (default: 32 for 256-bit)
  /// @return Hex-encoded token string
  static std::string GenerateTokenId(size_t bytes = 32);

  /// @brief Generate a session ID
  /// @param user_id User identifier for entropy
  /// @return Unique session ID
  static std::string GenerateSessionId(std::string_view user_id);

  /// @brief Generate a refresh token
  /// @return Hex-encoded refresh token
  static std::string GenerateRefreshToken();

  /// @brief Generate a password reset token
  /// @return Hex-encoded reset token
  static std::string GenerateResetToken();

  /// @brief Generate a device fingerprint
  /// @param device_id Raw device identifier
  /// @param platform Platform identifier
  /// @return Hashed device fingerprint
  static std::string HashDeviceId(std::string_view device_id, std::string_view platform);

  /// @brief Hash a token for storage (never store raw tokens)
  /// @param token The raw token
  /// @return SHA-256 hash of the token (hex-encoded)
  static std::string HashToken(std::string_view token);

  /// @brief Verify a token against its hash
  /// @param token The raw token to verify
  /// @param token_hash The stored hash
  /// @return true if token matches hash
  static bool VerifyTokenHash(std::string_view token, std::string_view token_hash);

  /// @brief Generate a JWT access token
  /// @param user_id User identifier
  /// @param secret JWT secret
  /// @param expires_in_seconds Token lifetime (default: 3600 = 1 hour)
  /// @return JWT string
  static std::string GenerateAccessToken(std::string_view user_id,
                                         std::string_view secret,
                                         int64_t expires_in_seconds = 3600);

  /// @brief Generate user ID from username/email
  /// @param identifier Username or email
  /// @return user_id in format "user_<hex>"
  static std::string GenerateUserId(std::string_view identifier);
};

} // namespace chirp::auth
