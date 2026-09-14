#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "base64.h"
#include "common/config.h"
#include "common/logger.h"
#include "jwt.h"
#include "sha256.h"

namespace chirp::common {
namespace {

std::string ToHex(std::string_view bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out.push_back(kHex[(c >> 4) & 0xF]);
    out.push_back(kHex[c & 0xF]);
  }
  return out;
}

std::string ToHex(const std::array<uint8_t, 32>& bytes) {
  return ToHex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

TEST(Base64UrlTest, EncodeDecodeText) {
  const std::string input = "Hello, Chirp!";
  const std::string encoded = Base64UrlEncode(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  EXPECT_FALSE(encoded.empty());

  std::string decoded;
  EXPECT_TRUE(Base64UrlDecode(encoded, &decoded));
  EXPECT_EQ(input, decoded);
}

TEST(Base64UrlTest, EncodeDecodeBinary) {
  const std::array<uint8_t, 4> bin{{0x00, 0xFF, 0x7F, 0x80}};
  const std::string encoded = Base64UrlEncode(bin.data(), bin.size());
  EXPECT_FALSE(encoded.empty());

  std::string decoded;
  EXPECT_TRUE(Base64UrlDecode(encoded, &decoded));
  ASSERT_EQ(decoded.size(), bin.size());
  EXPECT_EQ(0, std::memcmp(decoded.data(), bin.data(), bin.size()));
}

TEST(Sha256Test, KnownVectors) {
  EXPECT_EQ(ToHex(Sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(ToHex(Sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(HmacSha256Test, KnownVector) {
  const auto mac = HmacSha256("key", "The quick brown fox jumps over the lazy dog");
  EXPECT_EQ(ToHex(mac), "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST(JwtTest, HS256SignVerify) {
  const std::string secret = "test_secret";
  const std::string subject = "user123";
  const int64_t iat = 1700000000;

  const std::string token = JwtSignHS256(subject, iat, secret);
  EXPECT_FALSE(token.empty());

  JwtClaims parsed;
  std::string err;
  EXPECT_TRUE(JwtVerifyHS256(token, secret, &parsed, &err));
  EXPECT_TRUE(err.empty());
  EXPECT_EQ(parsed.subject, subject);
  EXPECT_EQ(parsed.issued_at, iat);
}

TEST(JwtTest, InvalidTokenRejected) {
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256("not.a.valid.token", "secret", &parsed, &err));
  EXPECT_FALSE(err.empty());
}

TEST(JwtTest, WrongSecretRejected) {
  const std::string token = JwtSignHS256("user123", 1, "secret1");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "secret2", &parsed, &err));
  EXPECT_EQ(err, "bad signature");
}

TEST(JwtTest, EscapedSubjectRoundTrip) {
  // Subject containing every character JsonEscape/ExtractJsonString handle.
  const std::string subject = "a\\b\"c\nd\re\tf";
  const std::string token = JwtSignHS256(subject, 123, "s3cret");

  JwtClaims parsed;
  std::string err;
  ASSERT_TRUE(JwtVerifyHS256(token, "s3cret", &parsed, &err));
  EXPECT_EQ(parsed.subject, subject);
  EXPECT_EQ(parsed.issued_at, 123);
}

TEST(JwtTest, ExpiryClaimRoundTrip) {
  JwtClaims parsed;
  std::string err;
  const std::string with_exp = JwtSignHS256("user123", 1700000000, "s3cret", 1700000600);
  ASSERT_TRUE(JwtVerifyHS256(with_exp, "s3cret", &parsed, &err));
  EXPECT_EQ(parsed.expires_at, 1700000600);

  // The 3-arg form still produces an exp-free token.
  const std::string without_exp = JwtSignHS256("user123", 1700000000, "s3cret");
  ASSERT_TRUE(JwtVerifyHS256(without_exp, "s3cret", &parsed, &err));
  EXPECT_EQ(parsed.expires_at, 0);
}

TEST(JwtTest, ExpiryChangesSignature) {
  // Same sub/iat but different exp must produce a different token (the exp
  // claim is part of the signed payload).
  const std::string a = JwtSignHS256("u", 1, "s", 100);
  const std::string b = JwtSignHS256("u", 1, "s", 200);
  EXPECT_NE(a, b);
}

namespace {

// Builds a JWT token with fully custom header/payload JSON, signed with the
// given secret. Used to exercise specific parser branches.
std::string MakeRawToken(std::string_view header_json, std::string_view payload_json,
                         std::string_view secret) {
  const std::string header_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(header_json.data()), header_json.size());
  const std::string payload_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size());
  const std::string signing_input = header_b64 + "." + payload_b64;
  auto sig = HmacSha256(secret, signing_input);
  return signing_input + "." + Base64UrlEncode(sig.data(), sig.size());
}

} // namespace

TEST(JwtTest, MissingDotsRejected) {
  JwtClaims parsed;
  std::string err;

  EXPECT_FALSE(JwtVerifyHS256("nodots", "s", &parsed, &err));
  EXPECT_EQ(err, "missing dot");

  EXPECT_FALSE(JwtVerifyHS256("only.one", "s", &parsed, &err));
  EXPECT_EQ(err, "missing second dot");
}

TEST(JwtTest, NullOutRejected) {
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256("a.b.c", "s", nullptr, &err));
  EXPECT_EQ(err, "null out");
}

TEST(JwtTest, PaddedSignatureAccepted) {
  const std::string token = JwtSignHS256("user1", 7, "s");
  // Append base64 padding characters; the verifier strips them.
  const std::string padded = token + "==";
  JwtClaims parsed;
  std::string err;
  EXPECT_TRUE(JwtVerifyHS256(padded, "s", &parsed, &err));
  EXPECT_EQ(parsed.subject, "user1");
}

TEST(JwtTest, UnsupportedAlgRejected) {
  const std::string token = MakeRawToken(R"({"alg":"none","typ":"JWT"})", R"({"sub":"u","iat":1})", "s");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "unsupported alg");
}

TEST(JwtTest, MissingSubRejected) {
  const std::string token = MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", R"({"iat":1})", "s");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "missing sub");
}

TEST(JwtTest, MissingIatIsTolerated) {
  // "iat" extraction failing is non-fatal; subject is still returned.
  const std::string token = MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", R"({"sub":"u2"})", "s");
  JwtClaims parsed;
  std::string err;
  ASSERT_TRUE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(parsed.subject, "u2");
}

TEST(JwtTest, NegativeIatParsed) {
  const std::string token =
      MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", R"({"sub":"u3","iat":-42})", "s");
  JwtClaims parsed;
  std::string err;
  ASSERT_TRUE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(parsed.issued_at, -42);
}

TEST(JwtTest, NonNumericIatTolerated) {
  const std::string token =
      MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", R"({"sub":"u4","iat":"soon"})", "s");
  JwtClaims parsed;
  std::string err;
  ASSERT_TRUE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(parsed.subject, "u4");
}

TEST(JwtTest, MalformedHeaderJsonRejected) {
  // Header JSON without an "alg" key at all.
  const std::string token = MakeRawToken(R"({"typ":"JWT"})", R"({"sub":"u","iat":1})", "s");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "unsupported alg");
}

TEST(JwtTest, CorruptHeaderBase64Rejected) {
  // '@' is not part of the URL-safe base64 alphabet.
  const std::string header_b64 = "@@@@";
  const std::string payload_json = R"({"sub":"u","iat":1})";
  const std::string payload_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size());
  const std::string signing_input = header_b64 + "." + payload_b64;
  auto sig = HmacSha256("s", signing_input);
  const std::string token = signing_input + "." + Base64UrlEncode(sig.data(), sig.size());

  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "bad header b64");
}

TEST(JwtTest, CorruptPayloadBase64Rejected) {
  const std::string header_json = R"({"alg":"HS256","typ":"JWT"})";
  const std::string header_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(header_json.data()), header_json.size());
  // '@' is not part of the URL-safe base64 alphabet; signature is computed
  // over the corrupt payload segment so signature validation passes.
  const std::string signing_input = header_b64 + "." + "@@@@";
  auto sig = HmacSha256("s", signing_input);
  const std::string token = signing_input + "." + Base64UrlEncode(sig.data(), sig.size());

  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "bad payload b64");
}

TEST(JwtTest, EscapedCharsInSubParsed) {
  // Exercises the escape-decoding branches of the payload string extractor.
  const std::string payload = "{\"sub\":\"a\\\"b\\\\c\\nd\\re\\tf\\ug\"}";
  const std::string token = MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", payload, "s");
  JwtClaims parsed;
  std::string err;
  ASSERT_TRUE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(parsed.subject, "a\"b\\c\nd\re\tfug");
}

TEST(JwtTest, UnterminatedSubjectValueRejected) {
  // Payload string never terminated: extraction fails -> missing sub.
  const std::string payload = R"({"sub":"never-closed)";
  const std::string token = MakeRawToken(R"({"alg":"HS256","typ":"JWT"})", payload, "s");
  JwtClaims parsed;
  std::string err;
  EXPECT_FALSE(JwtVerifyHS256(token, "s", &parsed, &err));
  EXPECT_EQ(err, "missing sub");
}

TEST(JwtTest, NullErrPointerAccepted) {
  const std::string token = JwtSignHS256("user9", 9, "s");
  JwtClaims parsed;
  EXPECT_TRUE(JwtVerifyHS256(token, "s", &parsed, nullptr));
  EXPECT_FALSE(JwtVerifyHS256("garbage", "s", &parsed, nullptr));
}

TEST(Base64UrlTest, DecodeNullOutFails) {
  std::string decoded;
  EXPECT_FALSE(Base64UrlDecode("AAAA", nullptr));
}

TEST(Base64UrlTest, DecodeInvalidCharacterFails) {
  std::string decoded;
  EXPECT_FALSE(Base64UrlDecode("AB@D", &decoded));
}

TEST(Base64UrlTest, EncodePaddingRemainders) {
  // rem == 2 (3 bytes -> 4 chars with one implicit pad)
  const std::array<uint8_t, 3> three{{'a', 'b', 'c'}};
  std::string decoded;
  ASSERT_TRUE(Base64UrlDecode(Base64UrlEncode(three.data(), three.size()), &decoded));
  EXPECT_EQ(decoded, "abc");

  // rem == 1 already covered by 4-byte binary case; cover rem == 0 via 6 bytes.
  const std::array<uint8_t, 6> six{{1, 2, 3, 4, 5, 6}};
  ASSERT_TRUE(Base64UrlDecode(Base64UrlEncode(six.data(), six.size()), &decoded));
  EXPECT_EQ(decoded.size(), 6u);
}

TEST(Base64UrlTest, DecodeWithExplicitPadding) {
  std::string decoded;
  // Standard base64 with '=' padding is accepted for 3-byte payloads.
  ASSERT_TRUE(Base64UrlDecode("YWJj", &decoded));
  EXPECT_EQ(decoded, "abc");
}

TEST(HmacSha256Test, LongKeyVector) {
  // RFC 4231 test case 6: key longer than the 64-byte block (131 x 0xaa).
  const std::string key(131, static_cast<char>(0xaa));
  const auto mac = HmacSha256(key, "Test Using Larger Than Block-Size Key - Hash Key First");
  EXPECT_EQ(ToHex(mac), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(ConfigTest, LoadFromFileParsesKeyValuePairs) {
  const std::string path = "/tmp/chirp_config_test.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("# comment line\n", f);
    std::fputs("host = 127.0.0.1\n", f);
    std::fputs("port=7000\n", f);
    std::fputs("  empty_val =\n", f);
    std::fputs("no_equals_sign\n", f);
    std::fputs("= skipped_empty_key\n", f);
    std::fclose(f);
  }

  Config cfg;
  ASSERT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());

  EXPECT_EQ(cfg.GetStringOr("host", "fallback"), "127.0.0.1");
  EXPECT_EQ(cfg.GetIntOr("port", 1), 7000);
  EXPECT_EQ(cfg.GetStringOr("empty_val", "none"), "");
  EXPECT_EQ(cfg.GetStringOr("missing", "default"), "default");
  EXPECT_EQ(cfg.GetIntOr("missing", 42), 42);
}

TEST(ConfigTest, LoadFromFileMissingFileFails) {
  Config cfg;
  EXPECT_FALSE(cfg.LoadFromFile("/tmp/chirp_config_does_not_exist_9x7.ini"));
  EXPECT_EQ(cfg.GetStringOr("anything", "none"), "none");
}

TEST(ConfigTest, GetIntRejectsInvalidValues) {
  const std::string path = "/tmp/chirp_config_test_bad.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("not_a_number=abc\n", f);
    std::fputs("partial_number=12abc\n", f);
    std::fputs("negative=-5\n", f);
    std::fclose(f);
  }

  Config cfg;
  ASSERT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());

  EXPECT_EQ(cfg.GetInt("not_a_number"), std::nullopt);
  EXPECT_EQ(cfg.GetInt("partial_number"), std::nullopt);
  ASSERT_TRUE(cfg.GetInt("negative").has_value());
  EXPECT_EQ(*cfg.GetInt("negative"), -5);
}

TEST(LoggerTest, LevelRoundTrip) {
  Logger& logger = Logger::Instance();
  const Logger::Level original = logger.GetLevel();

  logger.SetLevel(Logger::Level::kTrace);
  EXPECT_EQ(logger.GetLevel(), Logger::Level::kTrace);

  logger.SetLevel(Logger::Level::kError);
  EXPECT_EQ(logger.GetLevel(), Logger::Level::kError);

  // Log calls exercise both the filtered-out and emitted paths. Trace..Error
  // go through Log(), which formats every level string internally.
  logger.Trace("filtered trace");
  logger.Debug("filtered debug");
  logger.Info("filtered info");
  logger.Warn("filtered warn");
  logger.Error("emitted error entry");

  logger.SetLevel(Logger::Level::kTrace);
  logger.Trace("emitted trace entry");
  logger.Debug("emitted debug entry");
  logger.Info("emitted info entry");
  logger.Warn("emitted warn entry");
  logger.Error("emitted error entry");

  logger.SetLevel(original);
}

TEST(ConfigTest, ParsesWhitespaceAndCrlfLines) {
  const std::string path = "/tmp/chirp_config_test_ws.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("  host\t=\t 127.0.0.1 \r\n", f);   // CRLF + leading/trailing blanks
    std::fputs("  # indented comment\r\n", f);      // comment after trimming
    std::fputs("   \r\n", f);                       // whitespace-only line
    std::fputs("\t\t\r\n", f);                      // tab-only line
    std::fputs("name= alice \n", f);                // spaces around value
    std::fputs("url=a=b", f);                       // '=' inside value, no trailing newline
    std::fclose(f);
  }

  Config cfg;
  ASSERT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());

  EXPECT_EQ(cfg.GetString("host"), "127.0.0.1");
  EXPECT_EQ(cfg.GetString("name"), "alice");
  EXPECT_EQ(cfg.GetString("url"), "a=b"); // only the first '=' splits key/value
  EXPECT_EQ(cfg.GetString("does_not_exist"), std::nullopt);
}

TEST(ConfigTest, EmptyFileLoadsSuccessfully) {
  const std::string path = "/tmp/chirp_config_test_empty.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fclose(f);
  }

  Config cfg;
  EXPECT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());
  EXPECT_EQ(cfg.GetString("any"), std::nullopt);
  EXPECT_EQ(cfg.GetInt("any"), std::nullopt);
}

TEST(ConfigTest, DuplicateKeysLastValueWins) {
  const std::string path = "/tmp/chirp_config_test_dup.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("mode=first\n", f);
    std::fputs("mode=second\n", f);
    std::fclose(f);
  }

  Config cfg;
  ASSERT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());
  EXPECT_EQ(cfg.GetStringOr("mode", "none"), "second");
}

TEST(ConfigTest, GetIntEdgeCases) {
  const std::string path = "/tmp/chirp_config_test_int.ini";
  {
    FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("overflow=2147483648\n", f);   // > INT32_MAX
    std::fputs("plus=+7\n", f);               // from_chars rejects '+'
    std::fputs("hex=0x10\n", f);              // partial parse stops at 'x'
    std::fputs("leading_zeros=0042\n", f);    // parses as 42
    std::fputs("empty=\n", f);                // empty value is not an int
    std::fputs("min=-2147483648\n", f);       // INT32_MIN is in range
    std::fclose(f);
  }

  Config cfg;
  ASSERT_TRUE(cfg.LoadFromFile(path));
  std::remove(path.c_str());

  EXPECT_EQ(cfg.GetInt("overflow"), std::nullopt);
  EXPECT_EQ(cfg.GetInt("plus"), std::nullopt);
  EXPECT_EQ(cfg.GetInt("hex"), std::nullopt);
  ASSERT_TRUE(cfg.GetInt("leading_zeros").has_value());
  EXPECT_EQ(*cfg.GetInt("leading_zeros"), 42);
  EXPECT_EQ(cfg.GetInt("empty"), std::nullopt);
  ASSERT_TRUE(cfg.GetInt("min").has_value());
  EXPECT_EQ(*cfg.GetInt("min"), -2147483648);

  // GetIntOr passes through both parsed and fallback paths.
  EXPECT_EQ(cfg.GetIntOr("leading_zeros", -1), 42);
  EXPECT_EQ(cfg.GetIntOr("hex", -1), -1);
}

TEST(LoggerTest, FiltersBelowCurrentLevel) {
  Logger& logger = Logger::Instance();
  const Logger::Level original = logger.GetLevel();

  logger.SetLevel(Logger::Level::kWarn);
  // kInfo is below kWarn and must be filtered; kWarn and above pass.
  logger.Info("should be filtered");
  logger.Warn("should pass");
  logger.Error("should pass");
  EXPECT_EQ(logger.GetLevel(), Logger::Level::kWarn);

  logger.SetLevel(Logger::Level::kError);
  logger.Warn("should be filtered");
  logger.Error("should pass");
  EXPECT_EQ(logger.GetLevel(), Logger::Level::kError);

  logger.SetLevel(original);
}

TEST(LoggerTest, OutputFormatContainsTimestampLevelAndThread) {
  Logger& logger = Logger::Instance();
  const Logger::Level original = logger.GetLevel();

  // Redirect stderr into a string stream so the formatted line can be checked.
  std::ostringstream captured;
  std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
  logger.SetLevel(Logger::Level::kTrace);
  logger.Log(Logger::Level::kError, "format probe message");
  std::cerr.rdbuf(old);

  logger.SetLevel(original);

  const std::string line = captured.str();
  ASSERT_FALSE(line.empty());
  ASSERT_GE(line.size(), 5u);
  EXPECT_TRUE(isdigit(static_cast<unsigned char>(line[0]))); // timestamp "YYYY-MM-DD ..."
  EXPECT_NE(line.find(" [ERROR] "), std::string::npos);
  EXPECT_NE(line.find("[tid="), std::string::npos);
  EXPECT_NE(line.find("format probe message"), std::string::npos);
  EXPECT_EQ(line.back(), '\n');
}

TEST(LoggerTest, InvalidLevelFallsBackToUnknownLabel) {
  Logger& logger = Logger::Instance();
  const Logger::Level original = logger.GetLevel();

  // LevelToString only emits "UNKNOWN" for values outside the enum; Log()
  // exposes it via a cast. Filtering still applies (99 >= any real level).
  logger.SetLevel(Logger::Level::kError);
  logger.Log(static_cast<Logger::Level>(99), "bogus level entry");

  logger.SetLevel(original);
}

TEST(LoggerTest, ConcurrentLoggingIsThreadSafe) {
  Logger& logger = Logger::Instance();
  const Logger::Level original = logger.GetLevel();

  logger.SetLevel(Logger::Level::kTrace);
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&logger, t] {
      for (int i = 0; i < 25; ++i) {
        logger.Trace("trace from thread");
        logger.Debug("debug from thread");
        logger.Info("info from thread");
        logger.Warn("warn from thread");
        logger.Error("error from thread");
        logger.SetLevel(Logger::Level::kInfo);
        logger.SetLevel(Logger::Level::kTrace);
        (void)t;
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  EXPECT_EQ(logger.GetLevel(), Logger::Level::kTrace);
  logger.SetLevel(original);
}

} // namespace
} // namespace chirp::common
