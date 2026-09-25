// LoginTokenVerifier: HS256 login token verification for direct-entry
// entry (scaffold mode without a secret, exp-required JWT mode with one).

#include "login_token_verifier.h"

#include <gtest/gtest.h>

#include <string>

#include "common/jwt.h"

namespace {

using chirp::common::LoginTokenVerifier;

constexpr int64_t kNowMs = 1700000000000;  // fixed clock for deterministic tests

std::string SignToken(const std::string& subject, const std::string& secret,
                      int64_t expires_at_seconds) {
  return chirp::common::JwtSignHS256(subject, kNowMs / 1000 - 10, secret, expires_at_seconds);
}

TEST(LoginTokenVerifierTest, DisabledWithoutSecretKeepsScaffoldMode) {
  LoginTokenVerifier verifier("");
  EXPECT_FALSE(verifier.enabled());
}

TEST(LoginTokenVerifierTest, ValidUnexpiredTokenAccepted) {
  LoginTokenVerifier verifier("s3cret");
  ASSERT_TRUE(verifier.enabled());

  const std::string token = SignToken("alice", "s3cret", kNowMs / 1000 + 60);
  std::string user_id;
  std::string err;
  EXPECT_TRUE(verifier.Verify(token, kNowMs, &user_id, &err));
  EXPECT_EQ(user_id, "alice");
  EXPECT_TRUE(err.empty());
}

TEST(LoginTokenVerifierTest, ExpiredTokenRejected) {
  LoginTokenVerifier verifier("s3cret");
  const std::string token = SignToken("alice", "s3cret", kNowMs / 1000 - 1);
  std::string user_id;
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, &user_id, &err));
  EXPECT_EQ(err, "token expired");
}

TEST(LoginTokenVerifierTest, BoundaryExpiryIsAlreadyExpired) {
  LoginTokenVerifier verifier("s3cret");
  // exp == now (in seconds) is expired: the check is >=.
  const std::string token = SignToken("alice", "s3cret", kNowMs / 1000);
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, nullptr, &err));
}

TEST(LoginTokenVerifierTest, MissingExpiryClaimRejected) {
  LoginTokenVerifier verifier("s3cret");
  // The 3-arg sign form produces an exp-free token.
  const std::string token = chirp::common::JwtSignHS256("alice", kNowMs / 1000, "s3cret");
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, nullptr, &err));
  EXPECT_EQ(err, "token missing exp claim");
}

TEST(LoginTokenVerifierTest, WrongSecretRejected) {
  LoginTokenVerifier verifier("s3cret");
  const std::string token = SignToken("alice", "other-secret", kNowMs / 1000 + 60);
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, nullptr, &err));
  EXPECT_EQ(err, "bad signature");
}

TEST(LoginTokenVerifierTest, GarbageTokenRejected) {
  LoginTokenVerifier verifier("s3cret");
  std::string err;
  EXPECT_FALSE(verifier.Verify("garbage", kNowMs, nullptr, &err));
  EXPECT_FALSE(err.empty());
}

TEST(LoginTokenVerifierTest, EmptySubjectRejected) {
  LoginTokenVerifier verifier("s3cret");
  const std::string token = SignToken("", "s3cret", kNowMs / 1000 + 60);
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, nullptr, &err));
  EXPECT_EQ(err, "token missing sub claim");
}

// ---------------------------------------------------------------------------
// Optional out-params, sub-second expiry boundary and scaffold-mode hygiene.
// ---------------------------------------------------------------------------

TEST(LoginTokenVerifierTest, SuccessWithNullUserIdAccepted) {
  LoginTokenVerifier verifier("s3cret");
  const std::string token = SignToken("alice", "s3cret", kNowMs / 1000 + 60);
  std::string err = "stale error";
  // Callers that only need accept/reject pass a null user_id; success must
  // still report true and clear the stale error text.
  EXPECT_TRUE(verifier.Verify(token, kNowMs, nullptr, &err));
  EXPECT_TRUE(err.empty());
}

TEST(LoginTokenVerifierTest, FailurePathsTolerateNullErr) {
  LoginTokenVerifier verifier("s3cret");
  // Each of the three failure sites guards `if (err)`: missing exp, expired,
  // empty sub. None may dereference a null err pointer.
  const std::string no_exp = chirp::common::JwtSignHS256("alice", kNowMs / 1000, "s3cret");
  const std::string expired = SignToken("alice", "s3cret", kNowMs / 1000 - 1);
  const std::string no_sub = SignToken("", "s3cret", kNowMs / 1000 + 60);
  EXPECT_FALSE(verifier.Verify(no_exp, kNowMs, nullptr, nullptr));
  EXPECT_FALSE(verifier.Verify(expired, kNowMs, nullptr, nullptr));
  EXPECT_FALSE(verifier.Verify(no_sub, kNowMs, nullptr, nullptr));
}

TEST(LoginTokenVerifierTest, FailureLeavesUserIdUntouched) {
  LoginTokenVerifier verifier("s3cret");
  const std::string token = SignToken("alice", "s3cret", kNowMs / 1000 - 1);
  std::string user_id = "untouched";
  std::string err;
  EXPECT_FALSE(verifier.Verify(token, kNowMs, &user_id, &err));
  EXPECT_EQ(err, "token expired");
  EXPECT_EQ(user_id, "untouched");
}

TEST(LoginTokenVerifierTest, SubSecondExpiryBoundary) {
  LoginTokenVerifier verifier("s3cret");
  const int64_t exp = kNowMs / 1000 + 60;
  const std::string token = SignToken("alice", "s3cret", exp);
  std::string user_id;
  // One millisecond before exp: integer division still lands on exp - 1.
  EXPECT_TRUE(verifier.Verify(token, exp * 1000 - 1, &user_id, nullptr));
  EXPECT_EQ(user_id, "alice");
  // Exactly at exp*1000 the same token is already expired.
  user_id = "still-untouched";
  EXPECT_FALSE(verifier.Verify(token, exp * 1000, &user_id, nullptr));
  EXPECT_EQ(user_id, "still-untouched");
}

TEST(LoginTokenVerifierTest, ScaffoldModeVerifyRejectsPlainUserId) {
  LoginTokenVerifier verifier("");
  ASSERT_FALSE(verifier.enabled());
  // Callers gate on enabled(); Verify itself must still refuse a bare user
  // id instead of implicitly trusting it.
  std::string user_id;
  std::string err;
  EXPECT_FALSE(verifier.Verify("alice", kNowMs, &user_id, &err));
  EXPECT_TRUE(user_id.empty());
  EXPECT_FALSE(err.empty());
}

}  // namespace
