#pragma once

#include <string>
#include <optional>

namespace chirp::auth {

/// @brief Password hashing utility using Argon2id
/// Provides secure password storage with memory-hard hashing
class PasswordHasher {
public:
  /// @brief Hash a password using Argon2id
  /// @param password The plain text password
  /// @return The encoded password hash (includes salt and parameters)
  static std::string HashPassword(std::string_view password);

  /// @brief Verify a password against a hash
  /// @param password The plain text password to verify
  /// @param hash The stored password hash
  /// @return true if password matches, false otherwise
  static bool VerifyPassword(std::string_view password, std::string_view hash);

  /// @brief Validate password strength
  /// @param password The password to validate
  /// @return Error message if invalid, empty if valid
  static std::string ValidateStrength(std::string_view password);

  /// @brief Generate a random salt
  /// @return Base64-encoded random salt
  static std::string GenerateSalt();

  /// @brief Hash configuration parameters
  struct Config {
    uint32_t time_cost = 3;        // Number of iterations
    uint32_t memory_cost = 65536;   // Memory in KiB (64 MB)
    uint32_t parallelism = 4;       // Number of threads
    uint32_t salt_length = 16;      // Salt length in bytes
    uint32_t hash_length = 32;      // Hash length in bytes
  };

  /// @brief Get default configuration
  static const Config& DefaultConfig();

  /// @brief Set custom configuration
  static void SetConfig(const Config& config);
};

} // namespace chirp::auth
