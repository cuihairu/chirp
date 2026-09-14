// LoginTokenVerifier: HS256 login token verification for the chat direct
// entry (scaffold mode without a secret, exp-required JWT mode with one).

#include "login_token_verifier.h"

#include <gtest/gtest.h>

#include <string>

#include "common/jwt.h"

namespace {

using chirp::chat::LoginTokenVerifier;

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

}  // namespace
