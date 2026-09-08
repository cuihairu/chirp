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

TEST(JwtInternalJsonTest, ExtractIntNotANumberFails) {
  int64_t out = 0;
  EXPECT_FALSE(ExtractJsonInt64(R"({"k":abc})", "k", &out));
}

TEST(JwtInternalJsonTest, ExtractIntNegativeAndSpacingParses) {
  int64_t out = 0;
  EXPECT_TRUE(ExtractJsonInt64(R"({"x":1,"k" :  -12})", "k", &out));
  EXPECT_EQ(out, -12);
}

} // namespace
} // namespace chirp::common
