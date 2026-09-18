#pragma once

#include <string>

namespace chirp::common {

// Login token verification for a direct-entry edge (chat, social). With no
// secret configured, the scaffold "token is user_id" behavior stays (local
// dev and smokes). With a shared secret (--token_secret), the edge only
// accepts HS256 JWTs it can verify locally: signed with the same secret,
// carrying an "exp" claim still in the future, and a non-empty "sub" (the
// login user). Revocation is TTL-bounded by design; a revocation lookup
// (hybrid scheme) can be added later without a protocol change.
class LoginTokenVerifier {
 public:
  explicit LoginTokenVerifier(std::string secret) : secret_(std::move(secret)) {}

  // False means scaffold mode: the caller keeps treating the token as user_id.
  bool enabled() const { return !secret_.empty(); }

  // Verifies `token` at time `now_ms`. On success sets *user_id from the
  // "sub" claim; on failure sets *err and returns false.
  bool Verify(const std::string& token, int64_t now_ms, std::string* user_id,
              std::string* err) const;

 private:
  std::string secret_;
};

}  // namespace chirp::common
