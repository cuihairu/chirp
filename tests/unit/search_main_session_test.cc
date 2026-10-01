// Seam TU for services/search/src/main.cc: main() is renamed so the
// global-scope SignalHandler / running shutdown flag can be driven directly
// (batch 9-21 seam convention). Everything else in the file is main()
// scaffolding — argv, the 5-minute stats thread, the sleep loop — carried by
// the service's own suite (search_tests covers MessageSearchService itself).

#include <gtest/gtest.h>

#include <csignal>

#define main chirp_search_main
#include "main.cc"
#undef main

namespace {

TEST(SearchMainInternals, SignalHandlerClearsRunningFlag) {
  EXPECT_TRUE(running.load());
  SignalHandler(SIGTERM);
  EXPECT_FALSE(running.load());

  // Restore: the flag is TU-global and later cases in this binary may run
  // after this one.
  running.store(true);
}

TEST(SearchMainInternals, RunningFlagIsSettableAgain) {
  ASSERT_TRUE(running.load());
  SignalHandler(SIGINT);
  EXPECT_FALSE(running.load());
  running.store(true);
}

}  // namespace
