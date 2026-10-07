#include "text_segmenter.h"

#include <cstdint>

namespace chirp {
namespace search {

namespace {

// 解码 UTF-8 首字节给出的码点长度；非法首字节按 1 处理（原样透传，交由
// unicode61 当普通字节处理）。输入假定是合法 UTF-8（消息入库侧已按 UTF-8
// 文本处理）；这里只做长度判定，不做完整校验。
size_t Utf8SequenceLength(unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

// 三字节 UTF-8 码点（简化处理：本函数只被 len==3 的序列调用）。
uint32_t Decode3(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0] & 0x0F) << 12) |
         (static_cast<uint32_t>(p[1] & 0x3F) << 6) |
         static_cast<uint32_t>(p[2] & 0x3F);
}

// 四字节 UTF-8 码点。
uint32_t Decode4(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0] & 0x07) << 18) |
         (static_cast<uint32_t>(p[1] & 0x3F) << 12) |
         (static_cast<uint32_t>(p[2] & 0x3F) << 6) |
         static_cast<uint32_t>(p[3] & 0x3F);
}

bool IsCjkCodePoint(uint32_t cp) {
  return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x2A6DF);
}

}  // namespace

std::string SegmentForIndex(const std::string& utf8) {
  std::string out;
  out.reserve(utf8.size() + utf8.size() / 4);
  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
  const size_t n = utf8.size();
  size_t i = 0;
  while (i < n) {
    const size_t len = Utf8SequenceLength(p[i]);
    const size_t take = (i + len <= n) ? len : (n - i);
    out.append(reinterpret_cast<const char*>(p + i), take);
    bool split = false;
    if (take == 3) {
      split = IsCjkCodePoint(Decode3(p + i));
    } else if (take == 4) {
      split = IsCjkCodePoint(Decode4(p + i));
    }
    if (split) {
      out.push_back(' ');
    }
    i += take;
  }
  return out;
}

std::string BuildMatchPhrase(const std::string& keyword) {
  const std::string segmented = SegmentForIndex(keyword);
  // 「关键词可检索」判据：分词结果里至少有一个 unicode61 认定的 token 字符。
  // 与本文件的分段覆盖一致——ASCII 字母/数字，或被 SegmentForIndex 拆分的
  // CJK 码点（拉丁字母经 unicode61 折叠必成 token；全角/CJK 标点、空白都
  // 不是 token 字符）。逐码点判定，不能按「字节 >= 0x80」偷懒：全角标点
  // （EF.. 等 UTF-8 首字节）同样是高位字节。
  const unsigned char* p = reinterpret_cast<const unsigned char*>(segmented.data());
  const size_t n = segmented.size();
  bool has_token = false;
  size_t i = 0;
  while (i < n) {
    const unsigned char lead = p[i];
    if (lead < 0x80) {
      if ((lead >= 'a' && lead <= 'z') || (lead >= 'A' && lead <= 'Z') ||
          (lead >= '0' && lead <= '9')) {
        has_token = true;
        break;
      }
      ++i;
      continue;
    }
    size_t len = 1;
    uint32_t cp = lead;
    if ((lead & 0xE0) == 0xC0 && i + 1 < n) {
      len = 2;
      cp = lead & 0x1F;
      for (size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | (p[i + k] & 0x3F);
      }
    } else if ((lead & 0xF0) == 0xE0 && i + 2 < n) {
      len = 3;
      cp = lead & 0x0F;
      for (size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | (p[i + k] & 0x3F);
      }
    } else if ((lead & 0xF8) == 0xF0 && i + 3 < n) {
      len = 4;
      cp = lead & 0x07;
      for (size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | (p[i + k] & 0x3F);
      }
    }
    if (IsCjkCodePoint(cp)) {
      has_token = true;
      break;
    }
    i += len;
  }
  if (!has_token) {
    return {};
  }
  std::string out;
  out.reserve(segmented.size() + 2);
  out.push_back('"');
  for (char c : segmented) {
    if (c == '"') {
      out.push_back('"');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

size_t CountCodePoints(const std::string& utf8) {
  size_t count = 0;
  for (char c : utf8) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      ++count;
    }
  }
  return count;
}

}  // namespace search
}  // namespace chirp
