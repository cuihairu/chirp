#pragma once

// Header-only:测试与游戏工程直接 include 即用,无需改动任何构建清单。
// 行为对齐服务端 chirp::chat::WordFilter(词库格式、ASCII 大小写不敏感
// 子串匹配、mask 后重建的替换语义),套在 MessageInterceptor 的发送钩子
// 上做客户端预检。

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "message_interceptor.h"
#include "proto/chat.pb.h"

namespace chirp {
namespace sdk {

// 敏感词命中的两种客户端处置。没有 kRecord:记录待审核是服务端职责
// (服务端 WordFilter 已实现三级策略),客户端预检的职责只是不让脏词
// 出门——要么改写,要么拦截。
enum class WordFilterPolicy {
  kReplace,  // 命中区间改写为 replacement(连续命中塌缩成一次替换)
  kReject,   // 命中即拒绝:OnBeforeSend 返回 false,SDK 报 blocked
};

struct WordFilterOptions {
  // 词集合,大小写不敏感;构造时统一 lower + 去重。
  std::vector<std::string> terms;
  WordFilterPolicy policy = WordFilterPolicy::kReplace;
  std::string replacement = "**";
};

namespace detail {

// 只折叠 ASCII 大小写,与服务端 ToLower(默认 C locale)逐字节一致;
// UTF-8 多字节序列不受影响,按字节子串匹配仍然成立。
inline std::string AsciiLower(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace detail

// 按服务端词库文件格式(每行一词,# 开头为注释,空行忽略)逐行解析。
// 游戏把随包词库资源逐行喂进来,客户端与服务端即可共用同一份词库。
inline std::vector<std::string> ParseWordLexicon(
    const std::vector<std::string>& lines) {
  std::set<std::string> unique;
  for (const auto& raw : lines) {
    std::string term = raw;
    while (!term.empty() &&
           (term.back() == '\n' || term.back() == '\r' || term.back() == ' ')) {
      term.pop_back();
    }
    size_t start = 0;
    while (start < term.size() && term[start] == ' ') {
      ++start;
    }
    term = term.substr(start);
    if (term.empty() || term[0] == '#') {
      continue;
    }
    unique.insert(detail::AsciiLower(term));
  }
  return std::vector<std::string>(unique.begin(), unique.end());
}

// 敏感词预检拦截器。只滤发送侧:接收内容信任服务端已按其策略处理。
// terms 构造后不可变,OnBeforeSend 可并发只读调用。
class WordFilterInterceptor : public MessageInterceptor {
 public:
  explicit WordFilterInterceptor(WordFilterOptions options)
      : terms_(ParseWordLexicon(options.terms)), options_(std::move(options)) {}

  // kReplace:命中时把改写后的内容写回 msg 并放行;无命中原样放行。
  // kReject:命中返回 false(消息不会到达服务器,SDK 报 blocked)。
  bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) override {
    std::string rewritten;
    const auto result = Filter(msg.content(), &rewritten);
    if (result == FilterResult::kReplaced) {
      msg.set_content(rewritten);
    }
    return result != FilterResult::kBlocked;
  }

  size_t word_count() const { return terms_.size(); }

 private:
  enum class FilterResult { kPass, kReplaced, kBlocked };

  // kReplaced 时 *rewritten 为改写结果。
  FilterResult Filter(const std::string& content, std::string* rewritten) const {
    if (terms_.empty()) {
      return FilterResult::kPass;
    }
    const std::string lowered = detail::AsciiLower(content);
    std::vector<size_t> hit_starts;
    for (const auto& term : terms_) {
      // 词永不为空:ParseWordLexicon 丢掉空行。
      size_t pos = lowered.find(term);
      while (pos != std::string::npos) {
        hit_starts.push_back(pos);
        pos = lowered.find(term, pos + term.size());
      }
    }
    if (hit_starts.empty()) {
      return FilterResult::kPass;
    }
    if (options_.policy == WordFilterPolicy::kReject) {
      return FilterResult::kBlocked;
    }

    // kReplace:逐命中区间打 mask 再重建——连续命中塌缩成一次
    // replacement,未命中字节保留原大小写(与服务端 WordFilter 同款)。
    std::vector<bool> masked(lowered.size(), false);
    for (const auto& term : terms_) {
      size_t pos = lowered.find(term);
      while (pos != std::string::npos) {
        std::fill(masked.begin() + static_cast<long>(pos),
                  masked.begin() + static_cast<long>(pos + term.size()), true);
        pos = lowered.find(term, pos + term.size());
      }
    }
    std::string filtered;
    filtered.reserve(content.size());
    for (size_t i = 0; i < lowered.size();) {
      if (masked[i]) {
        filtered += options_.replacement;
        while (i < lowered.size() && masked[i]) {
          ++i;
        }
      } else {
        filtered.push_back(content[i]);
        ++i;
      }
    }
    *rewritten = std::move(filtered);
    return FilterResult::kReplaced;
  }

  std::vector<std::string> terms_;  // lower-cased, deduplicated, sorted
  WordFilterOptions options_;
};

}  // namespace sdk
}  // namespace chirp
