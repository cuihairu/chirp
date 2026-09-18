#include "login_token_verifier.h"

#include <cstdint>

#include "common/jwt.h"

namespace chirp::common {

bool LoginTokenVerifier::Verify(const std::string& token, int64_t now_ms,
                                std::string* user_id, std::string* err) const {
  chirp::common::JwtClaims claims;
  if (!chirp::common::JwtVerifyHS256(token, secret_, &claims, err)) {
    return false;
  }
  if (claims.expires_at <= 0) {
    if (err) {
      *err = "token missing exp claim";
    }
    return false;
  }
  if (now_ms / 1000 >= claims.expires_at) {
    if (err) {
      *err = "token expired";
    }
    return false;
  }
  if (claims.subject.empty()) {
    if (err) {
      *err = "token missing sub claim";
    }
    return false;
  }
  if (user_id) {
    *user_id = claims.subject;
  }
  return true;
}

}  // namespace chirp::common
