// Direct unit tests for internal JWT JSON helpers.
//
// The helpers live in an anonymous namespace inside jwt.cc, so this
// translation unit includes the implementation directly to gain access.
#include <gtest/gtest.h>

#include <string>

#include "jwt.cc"

namespace chirp::common {
namespace {

TEST(JwtInternalJsonTest, ExtractStringNullOutFails) {
  EXPECT_FALSE(ExtractJsonString(R"({"k":"v"})", "k", nullptr));
}

TEST(JwtInternalJsonTest, ExtractStringMissingKeyFails) {
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"other":"v"})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractStringMissingColonFails) {
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"k" "v"})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractStringTruncatedBeforeColonFails) {
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"k")", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractStringValueNotAStringFails) {
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"k" : 5})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractStringTruncatedBeforeValueFails) {
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"k" : )", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractStringWithSpacingParses) {
  std::string out;
  EXPECT_TRUE(ExtractJsonString(R"({"x": 1, "k" : "value"})", "k", &out));
  EXPECT_EQ(out, "value");
}

TEST(JwtInternalJsonTest, ExtractStringTrailingBackslashFails) {
  // Lone backslash at the end of the input: the escape guard sees pos at
  // the end, falls through, and the closing quote never arrives.
  std::string out;
  EXPECT_FALSE(ExtractJsonString(R"({"k":"abc\)", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntNullOutFails) {
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":5})", "k", nullptr));
}

TEST(JwtInternalJsonTest, ExtractIntMissingKeyFails) {
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"other":5})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntMissingColonFails) {
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k" 5})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntTruncatedFails) {
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":)", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntKeyAtEofFails) {
  // The key sits flush against the end: after the needle there is nothing
  // left to scan for the colon.
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k")", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntNotANumberFails) {
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":abc})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntNegativeAndSpacingParses) {
  int64_t out = 0;
  EXPECT_TRUE(ExtractJsonInt64(R"({"x":1,"k" :  -12})", "k", &out));
  EXPECT_EQ(out, -12);
}

// ---------------------------------------------------------------------------
// Value-side behaviour: escapes, mis-matched needles and out-param hygiene.
// ---------------------------------------------------------------------------

TEST(JwtInternalJsonTest, ExtractStringUnescapesEveryKnownEscape) {
  std::string out;
  ASSERT_TRUE(ExtractJsonString(R"({"k":"a\"b\\c\nd\re\tf"})", "k", &out));
  EXPECT_EQ(out, "a\"b\\c\nd\re\tf");
}

TEST(JwtInternalJsonTest, ExtractStringUnknownEscapeKeepsEscapedChar) {
  // The default arm keeps the escaped character verbatim instead of failing.
  std::string out;
  ASSERT_TRUE(ExtractJsonString(R"({"k":"x\zy"})", "k", &out));
  EXPECT_EQ(out, "xzy");
}

TEST(JwtInternalJsonTest, ExtractStringEscapedBackslashBeforeClosingQuote) {
  // "\\": the escaped backslash must not be mistaken for the closing quote.
  std::string out;
  ASSERT_TRUE(ExtractJsonString(R"({"k":"abc\\"})", "k", &out));
  EXPECT_EQ(out, "abc\\");
}

TEST(JwtInternalJsonTest, ExtractStringEmptyValueParses) {
  std::string out = "sentinel";
  ASSERT_TRUE(ExtractJsonString(R"({"k" : ""})", "k", &out));
  EXPECT_TRUE(out.empty());
}

TEST(JwtInternalJsonTest, ExtractStringAcceptsTabWhitespace) {
  std::string out;
  ASSERT_TRUE(ExtractJsonString("{\"k\"\t:\t\"v\"}", "k", &out));
  EXPECT_EQ(out, "v");
}

TEST(JwtInternalJsonTest, ExtractStringValueLookalikeDoesNotMatchKey) {
  // The needle "\"k\"" occurs as the *value* of "a"; the colon check after it
  // fails, so the naive search must not report a hit for key "k".
  std::string out = "sentinel";
  EXPECT_FALSE(ExtractJsonString(R"({"a":"k"})", "k", &out));
  EXPECT_EQ(out, "sentinel");
}

TEST(JwtInternalJsonTest, ExtractStringDuplicateKeyReturnsFirstValue) {
  std::string out;
  ASSERT_TRUE(ExtractJsonString(R"({"k":"first","k":"second"})", "k", &out));
  EXPECT_EQ(out, "first");
}

TEST(JwtInternalJsonTest, ExtractStringFailureLeavesOutUntouched) {
  std::string out = "sentinel";
  EXPECT_FALSE(ExtractJsonString(R"({"other":"v"})", "k", &out));
  EXPECT_EQ(out, "sentinel");
}

TEST(JwtInternalJsonTest, JsonEscapeEscapesSpecialCharacters) {
  EXPECT_EQ(JsonEscape("plain"), "plain");
  EXPECT_EQ(JsonEscape("a\\b\"c\nd\re\tf"), "a\\\\b\\\"c\\nd\\re\\tf");
}

TEST(JwtInternalJsonTest, ExtractIntStopsAtFirstNonDigit) {
  // The parser is deliberately naive: digits up to the first non-digit win.
  int64_t out = 0;
  ASSERT_TRUE(ExtractJsonInt64(R"({"k":12abc})", "k", &out));
  EXPECT_EQ(out, 12);
}

TEST(JwtInternalJsonTest, ExtractIntPlusSignRejected) {
  int64_t out = -1;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":+5})", "k", &out));
  EXPECT_EQ(out, -1);
}

TEST(JwtInternalJsonTest, ExtractIntLeadingZerosParse) {
  int64_t out = 0;
  ASSERT_TRUE(ExtractJsonInt64(R"({"k":007})", "k", &out));
  EXPECT_EQ(out, 7);
}

TEST(JwtInternalJsonTest, ExtractIntFailureLeavesOutUntouched) {
  int64_t out = -42;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":})", "k", &out));
  EXPECT_EQ(out, -42);
}

} // namespace
} // namespace chirp::common
