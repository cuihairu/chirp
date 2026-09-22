#pragma once

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

  // Re-reads the lexicon when its mtime moved and the check throttle
  // elapsed. Exposed for tests; Filter calls it internally.
  void ReloadIfStale(int64_t now_ms);

 private:
  WordFilterOptions options_;
  std::vector<std::string> terms_;  // lower-cased, deduplicated
  bool enabled_ = false;
  int64_t last_check_ms_ = 0;
  int64_t last_mtime_ms_ = 0;
};

} // namespace chirp::chat
