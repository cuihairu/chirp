#include "word_filter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <set>

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
// C++20 filesystem 而非 ::stat:glibc 的 st_mtim、BSD/macOS 的 st_mtimespec、
// MSVC 的 plain st_mtime 是三种方言,整体绕开。取 file_time_type 自身纪元
// 而非 to_sys 对齐系统时钟(MSVC 的文件时钟不是 std::chrono::file_clock,
// 没有 to_sys):本值只与同进程内上一次读数比相等,纪元平台各异无妨。
int64_t MtimeMs(const std::string& path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(std::filesystem::path(path), ec);
  if (ec) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             mtime.time_since_epoch())
      .count();
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
  ++version_;

  FILE* file = std::fopen(options_.lexicon_path.c_str(), "r");
  if (file == nullptr) {
    chirp::common::Logger::Instance().Warn("word filter lexicon not readable: " + options_.lexicon_path);
    last_mtime_ms_ = MtimeMs(options_.lexicon_path);
    if (on_reload_) {
      on_reload_(version_);
    }
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
  if (on_reload_) {
    on_reload_(version_);
  }
  return terms_.size();
}

std::string WordFilter::SerializeLexicon() const {
  std::string text;
  for (const auto& term : terms_) {
    text += term;
    text.push_back('\n');
  }
  return text;
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
