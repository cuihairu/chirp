#include "word_filter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>

#include <sys/stat.h>

#include "logger.h"

namespace chirp::chat {

namespace {

std::string ToLower(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// File mtime in milliseconds; 0 when the file cannot be stat'ed.
int64_t MtimeMs(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    return 0;
  }
  return static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
}

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

} // namespace

WordFilterPolicy WordFilterPolicyFromString(const std::string& policy) {
  const std::string lowered = ToLower(policy);
  if (lowered == "reject") {
    return WordFilterPolicy::kReject;
  }
  if (lowered == "record") {
    return WordFilterPolicy::kRecord;
  }
  return WordFilterPolicy::kReplace;
}

WordFilter::WordFilter(WordFilterOptions options) : options_(std::move(options)) {
  if (!options_.lexicon_path.empty()) {
    LoadLexicon();
  }
}

size_t WordFilter::LoadLexicon() {
  terms_.clear();
  enabled_ = false;

  FILE* file = std::fopen(options_.lexicon_path.c_str(), "r");
  if (file == nullptr) {
    chirp::common::Logger::Instance().Warn("word filter lexicon not readable: " + options_.lexicon_path);
    last_mtime_ms_ = MtimeMs(options_.lexicon_path);
    return 0;
  }

  std::set<std::string> unique;
  char line[512];
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    std::string term(line);
    while (!term.empty() && (term.back() == '\n' || term.back() == '\r' || term.back() == ' ')) {
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
    unique.insert(ToLower(term));
  }
  std::fclose(file);

  terms_.assign(unique.begin(), unique.end());
  last_mtime_ms_ = MtimeMs(options_.lexicon_path);
  enabled_ = !terms_.empty();
  chirp::common::Logger::Instance().Info("word filter lexicon loaded: " + options_.lexicon_path + " (" +
                          std::to_string(terms_.size()) + " terms)");
  return terms_.size();
}

void WordFilter::ReloadIfStale(int64_t now_ms) {
  if (now_ms - last_check_ms_ < options_.reload_check_interval_ms) {
    return;
  }
  last_check_ms_ = now_ms;
  const int64_t mtime = MtimeMs(options_.lexicon_path);
  if (mtime != 0 && mtime != last_mtime_ms_) {
    chirp::common::Logger::Instance().Info("word filter lexicon changed on disk, reloading");
    LoadLexicon();
  }
}

bool WordFilter::Filter(const std::string& sender_id, std::string* content) {
  if (!enabled()) {
    return true;
  }
  ReloadIfStale(NowMs());

  const std::string lowered = ToLower(*content);
  std::vector<size_t> hit_starts;
  for (const auto& term : terms_) {
    // Terms are never empty: LoadLexicon drops blank lines.
    size_t pos = lowered.find(term);
    while (pos != std::string::npos) {
      hit_starts.push_back(pos);
      pos = lowered.find(term, pos + term.size());
    }
  }
  if (hit_starts.empty()) {
    return true;
  }

  if (options_.policy == WordFilterPolicy::kRecord) {
    chirp::common::Logger::Instance().Warn("word filter recorded a hit from " + sender_id + " (" +
                            std::to_string(hit_starts.size()) + " term hit(s))");
    return true;
  }

  if (options_.policy == WordFilterPolicy::kReject) {
    chirp::common::Logger::Instance().Warn("word filter rejected a message from " + sender_id);
    return false;
  }

  // kReplace: mask every hit, then rebuild - masked runs collapse into one
  // replacement, unmasked bytes keep their original casing.
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
  filtered.reserve(content->size());
  for (size_t i = 0; i < lowered.size();) {
    if (masked[i]) {
      filtered += options_.replacement;
      while (i < lowered.size() && masked[i]) {
        ++i;
      }
    } else {
      filtered.push_back((*content)[i]);
      ++i;
    }
  }
  *content = filtered;
  chirp::common::Logger::Instance().Warn("word filter replaced a hit from " + sender_id);
  return true;
}

size_t WordFilter::word_count() const {
  return terms_.size();
}

} // namespace chirp::chat
