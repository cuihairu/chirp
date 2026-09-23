#include "gtest/gtest.h"

#include <algorithm>
#include <vector>

#include "proto/common.pb.h"

#include "delivery_prefs.h"

namespace chirp::chat {
namespace {

TEST(DeliveryPrefsTest, MuteableChannelSetIsWorldGuildTeam) {
  EXPECT_TRUE(IsMuteableChannel(WORLD));
  EXPECT_TRUE(IsMuteableChannel(GUILD));
  EXPECT_TRUE(IsMuteableChannel(TEAM));
  // Marquee and SYSTEM_CHANNEL are service broadcasts; private chat belongs
  // to the block list, not a channel mute.
  EXPECT_FALSE(IsMuteableChannel(PRIVATE));
  EXPECT_FALSE(IsMuteableChannel(MARQUEE));
  EXPECT_FALSE(IsMuteableChannel(SYSTEM_CHANNEL));
}

TEST(DeliveryPrefsTest, SetAndReadBack) {
  DeliveryPrefs prefs;
  EXPECT_TRUE(prefs.SetChannelMuted("alice", WORLD, true));
  EXPECT_TRUE(prefs.IsChannelMuted("alice", WORLD));
  // The other channels start unmuted.
  EXPECT_FALSE(prefs.IsChannelMuted("alice", GUILD));
  EXPECT_FALSE(prefs.IsChannelMuted("alice", TEAM));

  EXPECT_TRUE(prefs.SetChannelMuted("alice", TEAM, true));
  EXPECT_TRUE(prefs.IsChannelMuted("alice", TEAM));

  // Unmuting clears the flag again (idempotent set).
  EXPECT_FALSE(prefs.SetChannelMuted("alice", WORLD, false));
  EXPECT_FALSE(prefs.IsChannelMuted("alice", WORLD));
}

TEST(DeliveryPrefsTest, NonMuteableChannelsAreRefused) {
  DeliveryPrefs prefs;
  EXPECT_FALSE(prefs.SetChannelMuted("alice", PRIVATE, true));
  EXPECT_FALSE(prefs.SetChannelMuted("alice", MARQUEE, true));
  EXPECT_FALSE(prefs.SetChannelMuted("alice", SYSTEM_CHANNEL, true));
  // Nothing was recorded.
  EXPECT_FALSE(prefs.IsChannelMuted("alice", MARQUEE));
  EXPECT_TRUE(prefs.GetChannelMutes("alice").empty() == false);
  for (const auto& [channel_type, muted] : prefs.GetChannelMutes("alice")) {
    EXPECT_FALSE(muted) << "channel_type=" << channel_type;
  }
}

TEST(DeliveryPrefsTest, NonMuteableQueryIsAlwaysFalse) {
  DeliveryPrefs prefs;
  EXPECT_FALSE(prefs.IsChannelMuted("alice", MARQUEE));
}

TEST(DeliveryPrefsTest, UsersAreIsolated) {
  DeliveryPrefs prefs;
  prefs.SetChannelMuted("alice", GUILD, true);
  EXPECT_TRUE(prefs.IsChannelMuted("alice", GUILD));
  EXPECT_FALSE(prefs.IsChannelMuted("bob", GUILD));
  EXPECT_FALSE(prefs.IsChannelMuted("bob", WORLD));
}

TEST(DeliveryPrefsTest, GetChannelMutesListsAllThreeInOrder) {
  DeliveryPrefs prefs;
  // A user with no state: three unmuted entries.
  const auto fresh = prefs.GetChannelMutes("nobody");
  ASSERT_EQ(fresh.size(), 3u);
  EXPECT_EQ(fresh[0].first, WORLD);
  EXPECT_EQ(fresh[1].first, GUILD);
  EXPECT_EQ(fresh[2].first, TEAM);
  EXPECT_FALSE(fresh[0].second);

  prefs.SetChannelMuted("alice", GUILD, true);
  prefs.SetChannelMuted("alice", TEAM, true);
  const auto mixed = prefs.GetChannelMutes("alice");
  ASSERT_EQ(mixed.size(), 3u);
  EXPECT_EQ(mixed[0].first, WORLD);
  EXPECT_FALSE(mixed[0].second);
  EXPECT_EQ(mixed[1].first, GUILD);
  EXPECT_TRUE(mixed[1].second);
  EXPECT_EQ(mixed[2].first, TEAM);
  EXPECT_TRUE(mixed[2].second);
}

TEST(DeliveryPrefsTest, BlockHidesSendersMessages) {
  DeliveryPrefs prefs;
  EXPECT_TRUE(prefs.BlockUser("alice", "bob"));
  EXPECT_TRUE(prefs.IsUserBlocked("alice", "bob"));
  // The block is one-directional: bob blocking nobody still sees alice.
  EXPECT_FALSE(prefs.IsUserBlocked("bob", "alice"));

  EXPECT_TRUE(prefs.UnblockUser("alice", "bob"));
  EXPECT_FALSE(prefs.IsUserBlocked("alice", "bob"));
}

TEST(DeliveryPrefsTest, BlockRejectsEmptyTargetAndSelf) {
  DeliveryPrefs prefs;
  EXPECT_FALSE(prefs.BlockUser("alice", ""));
  EXPECT_FALSE(prefs.BlockUser("alice", "alice"));
  EXPECT_FALSE(prefs.IsUserBlocked("alice", "alice"));
  EXPECT_TRUE(prefs.GetBlockedUsers("alice").empty());
}

TEST(DeliveryPrefsTest, UnblockIsIdempotent) {
  DeliveryPrefs prefs;
  // Unblocking someone never blocked is a no-op success (only an empty
  // target is refused).
  EXPECT_TRUE(prefs.UnblockUser("alice", "bob"));
  EXPECT_FALSE(prefs.UnblockUser("alice", ""));

  // And it clears an existing block.
  EXPECT_TRUE(prefs.BlockUser("alice", "carol"));
  EXPECT_TRUE(prefs.UnblockUser("alice", "carol"));
  EXPECT_FALSE(prefs.IsUserBlocked("alice", "carol"));
}

TEST(DeliveryPrefsTest, BlockedUsersAreIsolatedPerUser) {
  DeliveryPrefs prefs;
  EXPECT_TRUE(prefs.BlockUser("alice", "bob"));
  // bob's own view is untouched: he never blocked anyone.
  EXPECT_FALSE(prefs.IsUserBlocked("bob", "alice"));
  EXPECT_TRUE(prefs.GetBlockedUsers("bob").empty());
}

TEST(DeliveryPrefsTest, GetBlockedUsersListsEveryBlockedSender) {
  DeliveryPrefs prefs;
  EXPECT_TRUE(prefs.GetBlockedUsers("nobody").empty());

  EXPECT_TRUE(prefs.BlockUser("alice", "bob"));
  EXPECT_TRUE(prefs.BlockUser("alice", "carol"));
  auto blocked = prefs.GetBlockedUsers("alice");
  // The set is unordered; compare as a set.
  std::sort(blocked.begin(), blocked.end());
  EXPECT_EQ(blocked, (std::vector<std::string>{"bob", "carol"}));
}

} // namespace
} // namespace chirp::chat
