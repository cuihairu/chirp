// 检索分词单测（message_search 批）：索引/查询两侧共用的 SegmentForIndex
// 与 FTS5 短语包装 BuildMatchPhrase、码点计数 CountCodePoints。
#include <gtest/gtest.h>

#include <string>

#include "text_segmenter.h"

namespace {

using chirp::search::BuildMatchPhrase;
using chirp::search::CountCodePoints;
using chirp::search::SegmentForIndex;

TEST(SearchTextSegmenter, LatinPassesThroughUnspaced) {
  // 拉丁字母不是 CJK：不加空格，交由 unicode61 按词切分。
  EXPECT_EQ(SegmentForIndex("hello world"), "hello world");
  EXPECT_EQ(SegmentForIndex(""), "");
}

TEST(SearchTextSegmenter, CjkGetsOneTokenPerChar) {
  // 每个汉字后插一个空格——unicode61 之下「每字一 token」，中文短语检索
  // 等价子串匹配。
  EXPECT_EQ(SegmentForIndex("你好"), "\xE4\xBD\xA0 \xE5\xA5\xBD ");
}

TEST(SearchTextSegmenter, MixedTextOnlySplitsCjk) {
  // 拉丁保持成词、CJK 逐字拆开：混合文本两种语义并存。
  EXPECT_EQ(SegmentForIndex("abc你d"), "abc\xE4\xBD\xA0 d");
}

TEST(SearchTextSegmenter, KanaAndExtensionBAlsoSplit) {
  // 平假名（3 字节）与 CJK 扩展 B（4 字节）同在覆盖区间。
  EXPECT_EQ(SegmentForIndex("\xE3\x81\x82"), "\xE3\x81\x82 ");           // あ
  EXPECT_EQ(SegmentForIndex("\xF0\xA0\x80\x8B"), "\xF0\xA0\x80\x8B ");   // U+2000B
}

TEST(SearchTextSegmenter, TruncatedTailPassesThrough) {
  // 截断的尾字节序列按剩余字节原样透传（不插空格、不死循环）。
  EXPECT_EQ(SegmentForIndex("a\xE4\xBD"), "a\xE4\xBD");
}

TEST(SearchTextSegmenter, MatchPhraseWrapsAndEscapes) {
  // 查询词同样分词后包引号；内部双引号翻倍转义（FTS5 短语语法）。
  EXPECT_EQ(BuildMatchPhrase("你好"),
            "\"\xE4\xBD\xA0 \xE5\xA5\xBD \"");
  EXPECT_EQ(BuildMatchPhrase("a\"b"), "\"a\"\"b\"");
}

TEST(SearchTextSegmenter, MatchPhraseEmptyWithoutTokenChars) {
  // 空串/纯标点/纯空白：无 token 字符，回空串（服务端据此回 INVALID_PARAM）。
  EXPECT_EQ(BuildMatchPhrase(""), "");
  EXPECT_EQ(BuildMatchPhrase("  \t"), "");
  EXPECT_EQ(BuildMatchPhrase("！？。，"), "");
  // 纯 CJK 标点（3000-303F）也不是 token 字符。
  EXPECT_EQ(BuildMatchPhrase("\xE3\x80\x82"), "");  // 。
}

TEST(SearchTextSegmenter, CountCodePointsMatchesUtf8Semantics) {
  EXPECT_EQ(CountCodePoints(""), 0u);
  EXPECT_EQ(CountCodePoints("abc"), 3u);
  EXPECT_EQ(CountCodePoints("你好"), 2u);
  EXPECT_EQ(CountCodePoints("a你b"), 3u);
}

TEST(SearchTextSegmenter, InvalidLeadBytePassesThroughAsOne) {
  // 0xF8-0xFF 不匹配任何 UTF-8 首字节前缀：按 1 字节透传（兜底臂），
  // 不插空格也不吞后续字节。
  const std::string in = "a\xFD" "b";
  EXPECT_EQ(SegmentForIndex(in), in);
  EXPECT_EQ(SegmentForIndex("\xF8\xF8"), "\xF8\xF8");
}

TEST(SearchTextSegmenter, MatchPhraseDigitCountsAsToken) {
  // 数字是 token 字符：词法扫描的数字臂。
  EXPECT_EQ(BuildMatchPhrase("v2"), "\"v2\"");
}

TEST(SearchTextSegmenter, MatchPhraseTwoByteLeadDecodedBeforeToken) {
  // é（2 字节序列）在扫描首位：逐码点解码后非 CJK，继续推进到 'c' 才定
  // token——2 字节解码臂，不能按「字节 >= 0x80」误判。
  EXPECT_EQ(BuildMatchPhrase("éclair"), "\"éclair\"");
}

TEST(SearchTextSegmenter, MatchPhraseFourByteCjkExtBToken) {
  // 𠀀（U+20000，4 字节）是 token 字符：4 字节解码臂 + CJK 判定。
  EXPECT_EQ(BuildMatchPhrase("\xF0\xA0\x80\x80"), "\"\xF0\xA0\x80\x80 \"");
}

TEST(SearchTextSegmenter, MatchPhraseFourByteNonCjkNotToken) {
  // 😀（U+1F600，4 字节非 CJK）：解码后不置 token，推进到 ASCII 'x'。
  EXPECT_EQ(BuildMatchPhrase("\xF0\x9F\x98\x80" "x"), "\"\xF0\x9F\x98\x80" "x\"");
}

}  // namespace
