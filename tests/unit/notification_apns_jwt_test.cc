// APNs provider token (ES256 JWT) tests: build/verify round trip, config
// validation, and the DER->raw conversion the JWS signature needs.
#include <gtest/gtest.h>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "apns_jwt.h"
#include "base64.h"

namespace chirp {
namespace app_notification {

namespace {

using common::Base64UrlDecode;

struct PkeyDeleter {
  void operator()(EVP_PKEY* k) const {
    if (k != nullptr) {
      EVP_PKEY_free(k);
    }
  }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;

PkeyPtr GenEcKey(const char* group) {
  return PkeyPtr(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", const_cast<char*>(group)));
}

// PKCS#8 "BEGIN PRIVATE KEY" — the .p8 encoding Apple's developer portal ships.
std::string Pkcs8Pem(EVP_PKEY* pkey) {
  BIO* bio = BIO_new(BIO_s_mem());
  const int ok = PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  char* data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  std::string pem(data, static_cast<size_t>(len));
  BIO_free(bio);
  return ok == 1 ? pem : "";
}

// Sign a message and return the DER ECDSA signature (test helper for the
// DER->raw conversion).
std::string SignDer(EVP_PKEY* pkey, const std::string& message) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  size_t len = 0;
  if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
      EVP_DigestSign(ctx, nullptr, &len,
                     reinterpret_cast<const uint8_t*>(message.data()),
                     message.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }
  std::string der(len, '\0');
  if (EVP_DigestSign(ctx, reinterpret_cast<uint8_t*>(&der[0]), &len,
                     reinterpret_cast<const uint8_t*>(message.data()),
                     message.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }
  EVP_MD_CTX_free(ctx);
  der.resize(len);
  return der;
}

// JWS raw r||s -> DER, the form EVP_DigestVerify consumes.
std::string RawToDer(const std::string& raw) {
  BIGNUM* r = BN_bin2bn(reinterpret_cast<const uint8_t*>(raw.data()),
                        static_cast<int>(raw.size() / 2), nullptr);
  BIGNUM* s = BN_bin2bn(
      reinterpret_cast<const uint8_t*>(raw.data() + raw.size() / 2),
      static_cast<int>(raw.size() / 2), nullptr);
  ECDSA_SIG* sig = ECDSA_SIG_new();
  ECDSA_SIG_set0(sig, r, s);  // takes ownership of r/s
  uint8_t* der = nullptr;
  const int len = i2d_ECDSA_SIG(sig, &der);
  std::string out(reinterpret_cast<const char*>(der), static_cast<size_t>(len));
  OPENSSL_free(der);
  ECDSA_SIG_free(sig);
  return out;
}

}  // namespace

TEST(ApnsProviderTokenTest, RoundTripVerifiesWithPublicKey) {
  const PkeyPtr key = GenEcKey("P-256");
  ASSERT_NE(key.get(), nullptr);
  const std::string pem = Pkcs8Pem(key.get());
  ASSERT_NE(pem.find("BEGIN PRIVATE KEY"), std::string::npos);

  std::string token;
  std::string error;
  ASSERT_TRUE(BuildApnsProviderToken(pem, "ABC123DEFG", "TEAM1234567",
                                     1700000000, &token, &error))
      << error;

  // Three dot-separated base64url segments; header and claims decode.
  const size_t header_end = token.find('.');
  const size_t claims_end = token.find('.', header_end + 1);
  ASSERT_NE(header_end, std::string::npos);
  ASSERT_NE(claims_end, std::string::npos);
  std::string header;
  std::string claims;
  std::string raw_sig;
  ASSERT_TRUE(Base64UrlDecode(token.substr(0, header_end), &header));
  ASSERT_TRUE(Base64UrlDecode(token.substr(header_end + 1, claims_end - header_end - 1), &claims));
  ASSERT_TRUE(Base64UrlDecode(token.substr(claims_end + 1), &raw_sig));
  EXPECT_EQ(header, "{\"alg\":\"ES256\",\"kid\":\"ABC123DEFG\"}");
  EXPECT_EQ(claims, "{\"iss\":\"TEAM1234567\",\"iat\":1700000000}");
  EXPECT_EQ(raw_sig.size(), 64u);

  // The signature verifies against the same key over "header.claims".
  std::unique_ptr<BIO, std::function<void(BIO*)>> bio(
      BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())),
      [](BIO* b) { if (b != nullptr) BIO_free(b); });
  EVP_PKEY* pub = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
  ASSERT_NE(pub, nullptr);
  const std::string signed_input = token.substr(0, claims_end);
  const std::string der = RawToDer(raw_sig);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  ASSERT_EQ(EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pub), 1);
  EXPECT_EQ(EVP_DigestVerify(ctx, reinterpret_cast<const uint8_t*>(der.data()),
                             der.size(),
                             reinterpret_cast<const uint8_t*>(signed_input.data()),
                             signed_input.size()),
            1);
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pub);
}

TEST(ApnsProviderTokenTest, EscapesKidAndTeamJson) {
  const PkeyPtr key = GenEcKey("P-256");
  ASSERT_NE(key.get(), nullptr);
  std::string token;
  std::string error;
  ASSERT_TRUE(BuildApnsProviderToken(Pkcs8Pem(key.get()), "a\"b\\c", "t\"1",
                                     1700000000, &token, &error))
      << error;
  const size_t header_end = token.find('.');
  std::string header;
  ASSERT_TRUE(Base64UrlDecode(token.substr(0, header_end), &header));
  EXPECT_EQ(header, "{\"alg\":\"ES256\",\"kid\":\"a\\\"b\\\\c\"}");
}

TEST(ApnsProviderTokenTest, RejectsMissingInputs) {
  const PkeyPtr key = GenEcKey("P-256");
  ASSERT_NE(key.get(), nullptr);
  const std::string pem = Pkcs8Pem(key.get());
  std::string token;
  std::string error;
  EXPECT_FALSE(BuildApnsProviderToken(pem, "", "TEAM", 1, &token, &error));
  EXPECT_FALSE(BuildApnsProviderToken(pem, "KID", "", 1, &token, &error));
  EXPECT_FALSE(BuildApnsProviderToken("", "KID", "TEAM", 1, &token, &error));
  EXPECT_FALSE(error.empty());
  // Null out-parameters are rejected outright.
  EXPECT_FALSE(BuildApnsProviderToken(pem, "KID", "TEAM", 1, nullptr, &error));
  EXPECT_FALSE(BuildApnsProviderToken(pem, "KID", "TEAM", 1, &token, nullptr));
}

TEST(ApnsProviderTokenTest, RejectsNonEcKey) {
  EVP_PKEY* rsa = EVP_RSA_gen(2048);
  ASSERT_NE(rsa, nullptr);
  std::string token;
  std::string error;
  EXPECT_FALSE(BuildApnsProviderToken(Pkcs8Pem(rsa), "KID", "TEAM", 1, &token, &error));
  EXPECT_NE(error.find("EC key"), std::string::npos);
  EVP_PKEY_free(rsa);
}

TEST(ApnsProviderTokenTest, RejectsWrongCurve) {
  const PkeyPtr key = GenEcKey("secp384r1");
  ASSERT_NE(key.get(), nullptr);
  std::string token;
  std::string error;
  EXPECT_FALSE(BuildApnsProviderToken(Pkcs8Pem(key.get()), "KID", "TEAM", 1, &token, &error));
  EXPECT_NE(error.find("P-256"), std::string::npos);
}

TEST(ApnsProviderTokenTest, RejectsGarbagePem) {
  std::string token;
  std::string error;
  EXPECT_FALSE(BuildApnsProviderToken("this is not a pem", "KID", "TEAM", 1, &token, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(token.empty());
}

TEST(DerEcdsaSigToRawTest, RoundTripsP256Signature) {
  const PkeyPtr key = GenEcKey("P-256");
  ASSERT_NE(key.get(), nullptr);
  const std::string der = SignDer(key.get(), "payload");
  ASSERT_FALSE(der.empty());
  std::string raw;
  ASSERT_TRUE(DerEcdsaSigToRaw(der, &raw));
  EXPECT_EQ(raw.size(), 64u);
}

TEST(DerEcdsaSigToRawTest, RejectsGarbageAndNullOut) {
  std::string raw;
  EXPECT_FALSE(DerEcdsaSigToRaw("", &raw));
  EXPECT_FALSE(DerEcdsaSigToRaw("\x01\x02\x03", &raw));
  EXPECT_FALSE(DerEcdsaSigToRaw("not der at all", &raw));
  const PkeyPtr key = GenEcKey("P-256");
  ASSERT_NE(key.get(), nullptr);
  EXPECT_FALSE(DerEcdsaSigToRaw(SignDer(key.get(), "payload"), nullptr));
}

TEST(DerEcdsaSigToRawTest, RejectsIntegersWiderThanP256) {
  // A secp384r1 signature carries 48-byte integers: not an ES256 signature.
  const PkeyPtr key = GenEcKey("secp384r1");
  ASSERT_NE(key.get(), nullptr);
  std::string raw;
  EXPECT_FALSE(DerEcdsaSigToRaw(SignDer(key.get(), "payload"), &raw));
}

TEST(ReadPemFileTest, RoundTripsAndReportsMissing) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "chirp_apns_jwt_test.pem";
  {
    std::ofstream out(path, std::ios::binary);
    out << "-----BEGIN PRIVATE KEY-----\nZm9v\n-----END PRIVATE KEY-----\n";
  }
  EXPECT_EQ(ReadPemFile(path.string()),
            "-----BEGIN PRIVATE KEY-----\nZm9v\n-----END PRIVATE KEY-----\n");
  std::filesystem::remove(path);
  EXPECT_EQ(ReadPemFile(path.string()), "");
}

}  // namespace app_notification
}  // namespace chirp
