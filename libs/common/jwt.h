#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chirp::common {

struct JwtClaims {
  std::string subject;
  int64_t issued_at{0};
};

// Creates a minimal HS256 JWT with {"sub":..., "iat":...}.
std::string JwtSignHS256(std::string_view subject, int64_t issued_at, std::string_view secret);

// Verifies HS256 signature and extracts claims. Returns false on invalid token.
bool JwtVerifyHS256(std::string_view token, std::string_view secret, JwtClaims* out, std::string* err);

} // namespace chirp::common

