#include "gtest/gtest.h"

#include "repeat_guard.h"

namespace chirp::chat {
namespace {

TEST(RepeatGuardTest, FirstTwoIdenticalSendsAreAllowed) {
  RepeatGuard guard;
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'000));
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'100));
}

TEST(RepeatGuardTest, ThirdIdenticalSendStartsTheMute) {
  RepeatGuard guard;
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'000));
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'100));
  // The trigger send itself is refused — it never reaches the channel.
  EXPECT_FALSE(guard.Allow("alice", "spam", 1'200));
}

TEST(RepeatGuardTest, MuteBlocksDifferentContentToo) {
  RepeatGuard guard;
  guard.Allow("alice", "spam", 1'000);
  guard.Allow("alice", "spam", 1'100);
  EXPECT_FALSE(guard.Allow("alice", "spam", 1'200));
  EXPECT_FALSE(guard.Allow("alice", "totally different", 1'300));
}

TEST(RepeatGuardTest, MuteExpiresAfterFiveMinutes) {
  RepeatGuard guard;
  const int64_t t0 = 1'000'000;
  guard.Allow("alice", "spam", t0);
  guard.Allow("alice", "spam", t0 + 1);
  EXPECT_FALSE(guard.Allow("alice", "spam", t0 + 2));
  // One ms before expiry: still muted.
  EXPECT_FALSE(guard.Allow("alice", "anything", t0 + 2 + kRepeatMuteMs - 1));
  // At expiry: allowed again, counting restarts at one.
  EXPECT_TRUE(guard.Allow("alice", "anything", t0 + 2 + kRepeatMuteMs));
  EXPECT_TRUE(guard.Allow("alice", "anything", t0 + 3 + kRepeatMuteMs));
  // Only after a third consecutive identical send does the mute re-arm.
  EXPECT_FALSE(guard.Allow("alice", "anything", t0 + 4 + kRepeatMuteMs));
}

TEST(RepeatGuardTest, DifferentContentResetsTheStreak) {
  RepeatGuard guard;
  EXPECT_TRUE(guard.Allow("alice", "a", 1'000));
  EXPECT_TRUE(guard.Allow("alice", "a", 1'100));
  EXPECT_TRUE(guard.Allow("alice", "b", 1'200));
  EXPECT_TRUE(guard.Allow("alice", "a", 1'300));
  EXPECT_TRUE(guard.Allow("alice", "a", 1'400));
  // Third consecutive identical "a": refused.
  EXPECT_FALSE(guard.Allow("alice", "a", 1'500));
}

TEST(RepeatGuardTest, UsersAreIndependent) {
  RepeatGuard guard;
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'000));
  EXPECT_TRUE(guard.Allow("alice", "spam", 1'100));
  // bob's identical streak is his own.
  EXPECT_TRUE(guard.Allow("bob", "spam", 1'200));
  EXPECT_TRUE(guard.Allow("bob", "spam", 1'300));
  EXPECT_FALSE(guard.Allow("alice", "spam", 1'400));
  EXPECT_FALSE(guard.Allow("bob", "spam", 1'500));
}

} // namespace
} // namespace chirp::chat
