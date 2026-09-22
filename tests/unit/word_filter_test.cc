#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "word_filter.h"

namespace {

using chirp::chat::WordFilter;
using chirp::chat::WordFilterOptions;
using chirp::chat::WordFilterPolicy;
using chirp::chat::WordFilterPolicyFromString;

// A lexicon file that removes itself when the test ends.
class TempLexicon {
 public:
  TempLexicon() {
    path_ = (std::filesystem::temp_directory_path() /
             ("chirp_word_filter_" + std::to_string(::getpid()) + "_" +
              std::to_string(reinterpret_cast<long>(this)) + ".txt"))
                .string();
  }
  ~TempLexicon() { std::remove(path_.c_str()); }

  TempLexicon(const TempLexicon&) = delete;
  TempLexicon& operator=(const TempLexicon&) = delete;

  const std::string& path() const { return path_; }

  void Write(const std::string& content) const {
    FILE* f = std::fopen(path_.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs(content.c_str(), f);
    std::fclose(f);
  }

  // Forces a fresh mtime regardless of the filesystem's timestamp
  // granularity, so hot-reload tests do not depend on wall-clock timing.
  void TouchNewMtime() const {
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_NOW;
    times[1].tv_sec = 1'700'000'000 + static_cast<long>(counter_++) * 10;
    times[1].tv_nsec = 0;
    ::utimensat(AT_FDCWD, path_.c_str(), times, 0);
  }

 private:
  std::string path_;
  mutable long counter_ = 0;
};

TEST(WordFilterTest, PolicyFromStringAcceptsTheThreeLevels) {
  EXPECT_EQ(WordFilterPolicyFromString("replace"), WordFilterPolicy::kReplace);
  EXPECT_EQ(WordFilterPolicyFromString("REJECT"), WordFilterPolicy::kReject);
  EXPECT_EQ(WordFilterPolicyFromString("Record"), WordFilterPolicy::kRecord);
  // Unknown spellings fall back to the least surprising default.
  EXPECT_EQ(WordFilterPolicyFromString("nonsense"), WordFilterPolicy::kReplace);
}

TEST(WordFilterTest, DisabledWithoutLexiconPathIsNoOp) {
  WordFilter filter(WordFilterOptions{});
  EXPECT_FALSE(filter.enabled());
  std::string content = "anything goes";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "anything goes");
  EXPECT_EQ(filter.word_count(), 0u);
}

TEST(WordFilterTest, MissingLexiconFileFailsOpen) {
  WordFilterOptions options;
  options.lexicon_path = "/nonexistent/chirp_lexicon.txt";
  WordFilter filter(options);
  // A broken lexicon must not take chat down: the filter runs empty.
  EXPECT_FALSE(filter.enabled());
  std::string content = "hello";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "hello");
}

TEST(WordFilterTest, LoadSkipsBlankLinesCommentsAndDuplicates) {
  TempLexicon file;
  file.Write("# comment\n\n  spaced  \nbadword\nBADWORD\nlast\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);
  EXPECT_TRUE(filter.enabled());
  // "spaced", the deduplicated "badword", and "last" survive.
  EXPECT_EQ(filter.word_count(), 3u);
}

TEST(WordFilterTest, ReplaceMasksHitsAndKeepsTheRest) {
  TempLexicon file;
  file.Write("badword\nevil\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);

  std::string content = "a BADWORD and an Evil plan";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "a ** and an ** plan");
}

TEST(WordFilterTest, ReplaceCollapsesAdjacentMaskedRunsIntoOneReplacement) {
  TempLexicon file;
  file.Write("bad\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);

  std::string content = "xbadbadbadx";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "x**x");
}

TEST(WordFilterTest, ReplaceMatchesChineseTermsByteWise) {
  TempLexicon file;
  file.Write("敏感词\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);

  std::string content = "这是敏感词内容";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "这是**内容");
}

TEST(WordFilterTest, RejectReturnsFalseAndLeavesContentUntouched) {
  TempLexicon file;
  file.Write("badword\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.policy = WordFilterPolicy::kReject;
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);

  std::string content = "a badword here";
  EXPECT_FALSE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "a badword here");
  // Clean content still passes under reject.
  std::string clean = "all good";
  EXPECT_TRUE(filter.Filter("sender", &clean));
}

TEST(WordFilterTest, RecordPassesContentThroughUnchanged) {
  TempLexicon file;
  file.Write("badword\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.policy = WordFilterPolicy::kRecord;
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);

  std::string content = "a badword here";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "a badword here");
}

TEST(WordFilterTest, HotReloadPicksUpLexiconChanges) {
  TempLexicon file;
  file.Write("oldterm\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 0;
  WordFilter filter(options);
  EXPECT_EQ(filter.word_count(), 1u);

  file.Write("newterm\n");
  file.TouchNewMtime();
  std::string content = "say newterm now";
  EXPECT_TRUE(filter.Filter("sender", &content));
  EXPECT_EQ(content, "say ** now");
  EXPECT_EQ(filter.word_count(), 1u);
}

TEST(WordFilterTest, ReloadCheckIsThrottledWithinTheInterval) {
  TempLexicon file;
  file.Write("oldterm\n");
  WordFilterOptions options;
  options.lexicon_path = file.path();
  options.reload_check_interval_ms = 60'000;
  WordFilter filter(options);
  ASSERT_EQ(filter.word_count(), 1u);

  file.Write("newterm\nsecond\n");
  file.TouchNewMtime();
  // Inside the throttle window the on-disk change is invisible; once the
  // window elapses, the next check reloads (two terms now).
  filter.ReloadIfStale(1'000);
  EXPECT_EQ(filter.word_count(), 1u);
  filter.ReloadIfStale(61'001);
  EXPECT_EQ(filter.word_count(), 2u);
}

} // namespace
