#ifndef CHIRP_SERVICES_SEARCH_TEXT_SEGMENTER_H_
#define CHIRP_SERVICES_SEARCH_TEXT_SEGMENTER_H_

#include <cstddef>
#include <string>

namespace chirp {
namespace search {

// 检索分词（message_search 批）：索引侧与查询侧必须逐字节同法，两侧都走
// SegmentForIndex，查询词再多一层 BuildMatchPhrase 的引号包装。
//
// 背景：FTS5 用内置 unicode61 tokenizer——拉丁/数字按词元切并折叠大小写，
// 但 CJK 字符被视作 token 字符且互不分隔，"你好世界"整段成为一个词元，
// 子串关键词永远失配。本分词器在每个 CJK 码点后插入一个空格，让 FTS5 按
// 「每字一 token」索引；查询词同样变换后包成 FTS5 短语，中文检索即等价
// 子串匹配，拉丁保持按词命中（词序敏感的短语语义）。
//
// 覆盖的 CJK 码点区间（其余字符原样透传，交由 unicode61 词元化）：
//   3040-30FF   平假名/片假名
//   3400-4DBF   CJK 扩展 A
//   4E00-9FFF   CJK 统一表意文字
//   F900-FAFF   CJK 兼容表意文字
//   20000-2A6DF CJK 扩展 B（UTF-8 四字节）
// 全角/CJK 标点（3000-303F 等）不是 token 字符，unicode61 自行切开，无需
// 在这里处理。

// 索引侧变换：在每个 CJK 码点后插入一个空格。
std::string SegmentForIndex(const std::string& utf8);

// 查询侧变换：SegmentForIndex 后包成 FTS5 MATCH 的带引号短语（内部双引号
// 翻倍转义）。关键词不含任何 token 字符（空串/纯标点/纯空白）时返回空串，
// 调用方据此回 INVALID_PARAM。
std::string BuildMatchPhrase(const std::string& keyword);

// UTF-8 码点计数（口径与 chat_validation 的内容长度校验一致：非 0b10xxxxxx
// 字节即一个新码点）。
size_t CountCodePoints(const std::string& utf8);

}  // namespace search
}  // namespace chirp

#endif  // CHIRP_SERVICES_SEARCH_TEXT_SEGMENTER_H_
