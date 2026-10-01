#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chirp::chat {

/// @brief What happens to a message whose content hits the lexicon
/// (game_chat_features P0 "敏感词过滤" three-level policy).
enum class WordFilterPolicy {
  kReplace,  // rewrite every hit as the replacement (default "**")
  kReject,   // refuse the message entirely (INVALID_PARAM)
  kRecord,   // deliver unchanged, log the hit for later review
};

/// @brief Parses "replace" / "reject" / "record" (case-insensitive).
/// Unknown strings fall back to kReplace.
WordFilterPolicy WordFilterPolicyFromString(const std::string& policy);

struct WordFilterOptions {
  // One lexicon term per line; blank lines and lines starting with '#' are
  // ignored. Empty path disables the filter entirely.
  std::string lexicon_path;
  WordFilterPolicy policy = WordFilterPolicy::kReplace;
  std::string replacement = "**";
  // The lexicon mtime is re-stat'ed at most once per interval (hot reload:
  // editing the file takes effect without a restart).
  int64_t reload_check_interval_ms = 5000;
};

/// @brief Lexicon-based content filter for user-sent chat messages.
/// Matching is plain case-insensitive substring search over the loaded
/// terms - fine for the hundreds-of-entries lexicons a game ships with.
///
/// Threading: intended to be called from the service io thread only (the
/// send path is single-threaded), so the lazy reload state is unsynchronized.
class WordFilter {
 public:
  explicit WordFilter(WordFilterOptions options);

  // Loads (or reloads) the lexicon file. Returns the number of terms kept.
  // A missing/unreadable file yields an empty lexicon and a warning log;
  // the filter keeps running (fail open) rather than taking chat down.
  size_t LoadLexicon();

  // Runs the configured policy over content. Returns false only when the
  // policy is kReject and the content hit the lexicon (the caller should
  // refuse the send). kReplace rewrites content in place; kRecord passes
  // content through after logging the hit. A disabled filter (no lexicon
  // path) is always a no-op true.
  bool Filter(const std::string& sender_id, std::string* content);

  bool enabled() const { return options_.lexicon_path.empty() ? false : enabled_; }
  size_t word_count() const;
  const WordFilterOptions& options() const { return options_; }

  // 词库下发协议（docs/design-notes/word_filter.md「词库下发协议」）：
  // version 每次装载自增（构造首载、mtime 热更新重载、重载失败变空表都算），
  // 从 1 起；从未装载（无 lexicon_path）为 0。
  int64_t version() const { return version_; }

  // 当前词库的规范化文本：每行一词、小写、去重排序（terms_ 的顺序），与
  // --word_filter_file 同格式，客户端 parseWordLexicon 可直接装载。
  std::string SerializeLexicon() const;

  // 装载完成后的回调（含热更新重载）；构造期间的首次装载不触发（回调此时
  // 还没挂）。io 线程同步调用——回调里只应 post 发送，不做重活。
  void set_on_reload(std::function<void(int64_t version)> callback) {
    on_reload_ = std::move(callback);
  }

  // Re-reads the lexicon when its mtime moved and the check throttle
  // elapsed. Exposed for tests; Filter calls it internally.
  void ReloadIfStale(int64_t now_ms);

 private:
  WordFilterOptions options_;
  std::vector<std::string> terms_;  // lower-cased, deduplicated
  bool enabled_ = false;
  int64_t last_check_ms_ = 0;
  int64_t last_mtime_ms_ = 0;
  int64_t version_ = 0;
  std::function<void(int64_t)> on_reload_;
};

} // namespace chirp::chat
