#include "gtest/gtest.h"

#include "channel_pacer.h"

namespace chirp::chat {
namespace {

TEST(ChannelPacerTest, MinSendIntervalCoversThePacedChannels) {
  EXPECT_EQ(MinSendIntervalMs(WORLD), 5000);
  EXPECT_EQ(MinSendIntervalMs(GUILD), 2000);
  EXPECT_EQ(MinSendIntervalMs(PRIVATE), 1000);
  // Coordination surfaces are unpaced.
  EXPECT_EQ(MinSendIntervalMs(TEAM), 0);
  EXPECT_EQ(MinSendIntervalMs(SYSTEM_CHANNEL), 0);
  EXPECT_EQ(MinSendIntervalMs(MARQUEE), 0);
}

TEST(ChannelPacerTest, FirstSendIsAlwaysAllowed) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 1'000));
  EXPECT_TRUE(pacer.Allow("alice", GUILD, 1'000));
  EXPECT_TRUE(pacer.Allow("alice", PRIVATE, 1'000));
}

TEST(ChannelPacerTest, SecondSendWithinTheIntervalIsRejected) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 1'000));
  // One millisecond short of the world interval.
  EXPECT_FALSE(pacer.Allow("alice", WORLD, 5'999));
}

TEST(ChannelPacerTest, SendAtExactlyTheIntervalIsAllowed) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 1'000));
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 6'000));
}

TEST(ChannelPacerTest, ChannelsArePacedIndependently) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 1'000));
  EXPECT_TRUE(pacer.Allow("alice", PRIVATE, 1'001));
  // The world window is still open even though the private send went through.
  EXPECT_FALSE(pacer.Allow("alice", WORLD, 1'002));
  EXPECT_TRUE(pacer.Allow("alice", PRIVATE, 2'500));
}

TEST(ChannelPacerTest, UsersArePacedIndependently) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", WORLD, 1'000));
  EXPECT_TRUE(pacer.Allow("bob", WORLD, 1'001));
  EXPECT_FALSE(pacer.Allow("alice", WORLD, 1'002));
}

TEST(ChannelPacerTest, RejectedAttemptDoesNotExtendTheWindow) {
  ChannelPacer pacer;
  EXPECT_TRUE(pacer.Allow("alice", GUILD, 1'000));
  // Spam attempts inside the window must not push the next allowed send out.
  EXPECT_FALSE(pacer.Allow("alice", GUILD, 1'500));
  EXPECT_FALSE(pacer.Allow("alice", GUILD, 2'900));
  EXPECT_TRUE(pacer.Allow("alice", GUILD, 3'000));
}

TEST(ChannelPacerTest, UnpacedChannelsNeverReject) {
  ChannelPacer pacer;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(pacer.Allow("alice", MARQUEE, i));
    EXPECT_TRUE(pacer.Allow("alice", TEAM, i));
    EXPECT_TRUE(pacer.Allow("alice", SYSTEM_CHANNEL, i));
  }
}

} // namespace
} // namespace chirp::chat
