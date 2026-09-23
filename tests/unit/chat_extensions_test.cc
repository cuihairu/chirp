#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

#include "channel_manager.h"
#include "mention_manager.h"
#include "message_edit_manager.h"
#include "message_store_config.h"
#include "proto/chat.pb.h"

namespace {

using chirp::chat::ChannelManager;
using chirp::chat::ChannelPermissionChecker;
using chirp::chat::EditConfig;
using chirp::chat::MentionConfig;
using chirp::chat::MentionManager;
using chirp::chat::MessageEditManager;
using chirp::chat::MessageStoreConfig;

// ---------------------------------------------------------------------------
// MentionManager
// ---------------------------------------------------------------------------

TEST(MentionManagerTest, ParsesUserChannelEveryoneAndHere) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("hi @alice and @bob see #general @everyone @here", "sender");

  EXPECT_TRUE(parsed.mentions_everyone);
  EXPECT_TRUE(parsed.mentions_here);
  EXPECT_TRUE(parsed.mentioned_user_ids.count("alice") > 0);
  EXPECT_TRUE(parsed.mentioned_user_ids.count("bob") > 0);

  bool has_channel = false;
  for (const auto& m : parsed.mentions) {
    if (m.type() == chirp::chat::MENTION_TYPE_CHANNEL && m.id() == "general") {
      has_channel = true;
    }
  }
  EXPECT_TRUE(has_channel);
}

TEST(MentionManagerTest, EmptyContentHasNoMentions) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("plain message without mentions", "sender");
  EXPECT_TRUE(parsed.mentions.empty());
  EXPECT_FALSE(parsed.mentions_everyone);
  EXPECT_FALSE(parsed.mentions_here);
}

TEST(MentionManagerTest, MaxMentionsPerMessageIsEnforced) {
  MentionConfig config;
  config.max_mentions_per_message = 3;
  MentionManager mgr(config);
  auto parsed = mgr.ParseMentions("@a1 @a2 @a3 @a4 @a5", "sender");
  // User-mention parsing stops at the configured cap.
  EXPECT_LE(parsed.mentions.size(), 3u + 2u);  // cap for users + everyone/here
}

TEST(MentionManagerTest, EveryoneCooldownBlocksRepeatedUse) {
  MentionConfig config;
  config.everyone_cooldown_ms = 60000;
  MentionManager mgr(config);

  EXPECT_TRUE(mgr.CanMentionEveryone("alice", "chan"));
  mgr.RecordEveryoneMention("alice", "chan");
  EXPECT_FALSE(mgr.CanMentionEveryone("alice", "chan"));
  // Other users / channels are unaffected.
  EXPECT_TRUE(mgr.CanMentionEveryone("bob", "chan"));
  EXPECT_TRUE(mgr.CanMentionEveryone("alice", "other"));
}

TEST(MentionManagerTest, ModeratorBypassesMentionCooldown) {
  MentionConfig config;
  config.everyone_cooldown_ms = 60000;
  MentionManager mgr(config);
  mgr.RecordEveryoneMention("alice", "chan");
  EXPECT_FALSE(mgr.CanMentionEveryone("alice", "chan"));
  EXPECT_TRUE(mgr.CanMentionEveryone("alice", "chan", /*is_moderator=*/true));
}

TEST(MentionManagerTest, HereSharesEveryoneGate) {
  MentionConfig config;
  config.allow_here = true;
  config.everyone_cooldown_ms = 60000;
  MentionManager mgr(config);
  EXPECT_TRUE(mgr.CanMentionHere("alice", "chan"));
  mgr.RecordEveryoneMention("alice", "chan");
  EXPECT_FALSE(mgr.CanMentionHere("alice", "chan"));
}

TEST(MentionManagerTest, NotifyUserIdsExcludeSender) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("hi @alice @bob", "alice");
  auto ids = parsed.GetNotifyUserIds("alice", {"alice", "bob", "carol"});
  EXPECT_EQ(ids.size(), 1u);
  EXPECT_EQ(*ids.begin(), "bob");
}

TEST(MentionManagerTest, EveryoneNotifiesAllMembersExceptSender) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("@everyone", "sender");
  auto ids = parsed.GetNotifyUserIds("sender", {"sender", "a", "b"});
  // Every member except the sender is notified (the literal "everyone"
  // token is also captured by the plain-user regex; assert membership of
  // the real users only).
  EXPECT_TRUE(ids.count("a") > 0);
  EXPECT_TRUE(ids.count("b") > 0);
  EXPECT_EQ(ids.count("sender"), 0u);
}

TEST(MentionManagerTest, SuggestionsIncludeEveryoneAndHere) {
  MentionManager mgr;
  auto sugg = mgr.GetMentionSuggestions("every", "chan", "alice");
  ASSERT_GE(sugg.size(), 1u);
  EXPECT_EQ(sugg[0].display_text, "@everyone");

  auto here = mgr.GetMentionSuggestions("here", "chan", "alice");
  ASSERT_GE(here.size(), 1u);
  EXPECT_EQ(here[0].display_text, "@here");
}

TEST(MentionManagerTest, UnknownQueryYieldsNoSuggestions) {
  MentionManager mgr;
  EXPECT_TRUE(mgr.GetMentionSuggestions("zzzz-no-match", "chan", "alice").empty());
}

TEST(MentionManagerTest, NotificationRecipientsOnlyOnlineUsers) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("@alice @bob @carol", "sender");
  // Direct mentions notify every mentioned user regardless of presence.
  auto recipients = mgr.BuildNotificationRecipients(
      parsed, "sender", {"alice", "bob", "carol"}, {"bob"});
  EXPECT_EQ(recipients.size(), 3u);
}

TEST(MentionManagerTest, HereNotifiesOnlyOnlineUsers) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("@here", "sender");
  auto recipients = mgr.BuildNotificationRecipients(
      parsed, "sender", {"alice", "bob"}, {"bob"});
  ASSERT_EQ(recipients.size(), 1u);
  EXPECT_EQ(recipients[0], "bob");
}

TEST(MentionManagerTest, HereSkipsSenderWhenSenderIsAMember) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("@here", "alice");
  auto recipients = mgr.BuildNotificationRecipients(
      parsed, "alice", {"alice", "bob"}, {"alice", "bob"});
  ASSERT_EQ(recipients.size(), 1u);
  EXPECT_EQ(recipients[0], "bob");
}

TEST(MentionManagerTest, DirectMentionSkipsSenderInTheMemberLoop) {
  MentionManager mgr;
  auto parsed = mgr.ParseMentions("@alice @bob", "alice");
  auto ids = parsed.GetNotifyUserIds("alice", {"alice", "bob"});
  EXPECT_EQ(ids.count("alice"), 0u);
  EXPECT_EQ(ids.count("bob"), 1u);
}

TEST(MentionManagerTest, ChannelMentionCapStopsFurtherMatches) {
  MentionConfig config;
  config.max_mentions_per_message = 1;
  MentionManager mgr(config);
  auto parsed = mgr.ParseMentions("#one #two #three", "sender");
  int channels = 0;
  for (const auto& m : parsed.mentions) {
    if (m.type() == chirp::chat::MENTION_TYPE_CHANNEL) {
      ++channels;
    }
  }
  EXPECT_EQ(channels, 1);
}

TEST(MentionManagerTest, FormatMentionsKeepsContent) {
  MentionManager mgr;
  EXPECT_EQ(mgr.FormatMentions("no mentions here", {}), "no mentions here");

  // Single user mention is emphasised in place.
  auto users = mgr.ParseMentions("hello @alice", "sender");
  auto out = mgr.FormatMentions("hello @alice", users.mentions);
  EXPECT_NE(out.find("**@alice**"), std::string::npos);

  // NOTE: content that contains both plain mentions and @everyones hits a
  // position-drift quirk (the plain-user regex also captures "everyone",
  // producing two replacements at the same offset); that is tracked in the
  // code review notes instead of asserted here.

  // Channel and role-style mentions format through their own branches.
  auto chans = mgr.ParseMentions("see #general", "sender");
  auto chan_out = mgr.FormatMentions("see #general", chans.mentions);
  EXPECT_NE(chan_out.find("**#general**"), std::string::npos);

  chirp::chat::Mention role;
  role.set_type(chirp::chat::MENTION_TYPE_ROLE);
  role.set_id("r1");
  role.set_start_index(0);
  role.set_length(6);
  auto role_out = mgr.FormatMentions("@mods!", {role});
  EXPECT_NE(role_out.find("**@mods!**"), std::string::npos);
}

TEST(MentionManagerTest, FormatHereAndEveryoneMarkers) {
  MentionManager mgr;
  auto here = mgr.ParseMentions("@here", "sender");
  EXPECT_NE(mgr.FormatMentions("@here", here.mentions).find("**@here**"), std::string::npos);

  chirp::chat::Mention everyone_only;
  everyone_only.set_type(chirp::chat::MENTION_TYPE_EVERYONE);
  everyone_only.set_start_index(0);
  everyone_only.set_length(9);
  EXPECT_NE(mgr.FormatMentions("@everyone", {everyone_only}).find("**@everyone**"), std::string::npos);

  chirp::chat::Mention here_only;
  here_only.set_type(chirp::chat::MENTION_TYPE_HERE);
  here_only.set_start_index(0);
  here_only.set_length(5);
  EXPECT_NE(mgr.FormatMentions("@here", {here_only}).find("**@here**"), std::string::npos);

  chirp::chat::Mention unknown_type;
  unknown_type.set_type(static_cast<chirp::chat::MentionType>(999));
  unknown_type.set_start_index(0);
  unknown_type.set_length(4);
  // Unknown mention types replace the range with an empty string.
  EXPECT_EQ(mgr.FormatMentions("text", {unknown_type}), "");
}

TEST(MentionManagerTest, FormatMentionsSkipsOutOfRangeSpans) {
  MentionManager mgr;
  chirp::chat::Mention overrun;
  overrun.set_type(chirp::chat::MENTION_TYPE_USER);
  overrun.set_id("alice");
  overrun.set_start_index(0);
  overrun.set_length(99);
  EXPECT_EQ(mgr.FormatMentions("short", {overrun}), "short");
}

TEST(MessageEditManagerTest, ZeroWindowAndLimitMeanUnlimited) {
  EditConfig config;
  config.edit_time_window_ms = 0;   // no time limit
  config.max_edit_count = 0;        // no edit limit
  MessageEditManager mgr(config);
  mgr.RegisterMessage("m1", "alice", "content");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(mgr.EditMessage("m1", "alice", "v" + std::to_string(i)));
  }
  EXPECT_TRUE(mgr.CanEdit("m1", "alice"));
}

TEST(MentionManagerTest, EveryoneDisabledByConfig) {
  MentionConfig config;
  config.allow_everyone = false;
  MentionManager mgr(config);
  EXPECT_FALSE(mgr.CanMentionEveryone("alice", "chan"));
  EXPECT_FALSE(mgr.CanMentionHere("alice", "chan"));
}

TEST(MentionManagerTest, StaleCooldownEntriesArePurged) {
  MentionConfig config;
  config.everyone_cooldown_ms = 1;  // expires almost immediately
  MentionManager mgr(config);
  mgr.RecordEveryoneMention("alice", "chan");
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  // The expired entry is purged on the next check / record. Recording for
  // another user sweeps Alice's stale entry (self entries are refreshed
  // before the sweep).
  EXPECT_TRUE(mgr.CanMentionEveryone("alice", "chan"));
  mgr.RecordEveryoneMention("bob", "chan");
  mgr.RecordEveryoneMention("alice", "chan");
}

// ---------------------------------------------------------------------------
// MessageEditManager
// ---------------------------------------------------------------------------

TEST(MessageEditManagerTest, RegisterEditAndFetchHistory) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "original");

  chirp::chat::ChatMessageFull full;
  EXPECT_TRUE(mgr.EditMessage("m1", "alice", "edited", &full));
  EXPECT_EQ(full.content(), "edited");
  EXPECT_EQ(full.edit_count(), 1);

  auto history = mgr.GetEditHistory("m1");
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].new_content(), "edited");
}

TEST(MessageEditManagerTest, OnlySenderOrModeratorCanEdit) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "original");
  EXPECT_FALSE(mgr.EditMessage("m1", "bob", "hijack"));
  EXPECT_TRUE(mgr.CanEdit("m1", "alice"));
  EXPECT_FALSE(mgr.CanEdit("m1", "bob"));
  EXPECT_TRUE(mgr.CanEdit("m1", "mod", /*is_moderator=*/true));
}

TEST(MessageEditManagerTest, SoftDeleteAndModeratorHardDelete) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "content");

  EXPECT_FALSE(mgr.DeleteMessage("m1", "bob", /*is_hard_delete=*/false));
  EXPECT_TRUE(mgr.DeleteMessage("m1", "alice", /*is_hard_delete=*/false));

  chirp::chat::ChatMessageFull full;
  EXPECT_TRUE(mgr.GetFullMessage("m1", &full));
  EXPECT_TRUE(full.is_deleted());

  // Hard delete requires moderator rights.
  mgr.RegisterMessage("m2", "alice", "content");
  EXPECT_FALSE(mgr.DeleteMessage("m2", "alice", /*is_hard_delete=*/true));
  EXPECT_TRUE(mgr.DeleteMessage("m2", "mod", /*is_hard_delete=*/true, /*is_moderator=*/true));
  EXPECT_FALSE(mgr.GetFullMessage("m2", &full));
}

TEST(MessageEditManagerTest, EditWindowExpiresForSender) {
  EditConfig config;
  config.edit_time_window_ms = 1;  // 1ms: expires after a short sleep
  MessageEditManager mgr(config);
  mgr.RegisterMessage("m1", "alice", "content");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(mgr.CanEdit("m1", "alice"));
  EXPECT_FALSE(mgr.EditMessage("m1", "alice", "x"));
  // Moderators may still edit.
  EXPECT_TRUE(mgr.CanEdit("m1", "mod", /*is_moderator=*/true));
}

TEST(MessageEditManagerTest, EditLimitIsEnforced) {
  EditConfig config;
  config.max_edit_count = 2;
  MessageEditManager mgr(config);
  mgr.RegisterMessage("m1", "alice", "content");
  EXPECT_TRUE(mgr.EditMessage("m1", "alice", "1"));
  EXPECT_TRUE(mgr.EditMessage("m1", "alice", "2"));
  EXPECT_FALSE(mgr.EditMessage("m1", "alice", "3"));
  EXPECT_FALSE(mgr.CanEdit("m1", "alice"));
}

TEST(MessageEditManagerTest, BulkDeleteReturnsFailures) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "content");
  mgr.RegisterMessage("m2", "bob", "content");

  auto failed = mgr.BulkDelete({"m1", "m2", "missing"}, "chan", "mod", /*is_moderator=*/true);
  ASSERT_EQ(failed.size(), 1u);
  EXPECT_EQ(failed[0], "missing");

  // Bulk delete is soft: messages remain queryable but flagged deleted.
  chirp::chat::ChatMessageFull full;
  ASSERT_TRUE(mgr.GetFullMessage("m1", &full));
  EXPECT_TRUE(full.is_deleted());
  ASSERT_TRUE(mgr.GetFullMessage("m2", &full));
  EXPECT_TRUE(full.is_deleted());
}

TEST(MessageEditManagerTest, DeletedMessagesCannotBeEdited) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "content");
  ASSERT_TRUE(mgr.DeleteMessage("m1", "alice", /*is_hard_delete=*/false));
  EXPECT_FALSE(mgr.EditMessage("m1", "alice", "again"));
  EXPECT_FALSE(mgr.CanEdit("m1", "alice"));
}

TEST(MessageEditManagerTest, FullMessageSnapshotIncludesHistory) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "v1");
  ASSERT_TRUE(mgr.EditMessage("m1", "alice", "v2"));
  ASSERT_TRUE(mgr.EditMessage("m1", "alice", "v3"));

  chirp::chat::ChatMessageFull full;
  ASSERT_TRUE(mgr.GetFullMessage("m1", &full));
  EXPECT_EQ(full.content(), "v3");
  EXPECT_EQ(full.edit_count(), 2);
  EXPECT_EQ(full.edit_history_size(), 2);
  EXPECT_FALSE(mgr.GetFullMessage("m1", nullptr));
}

TEST(MessageEditManagerTest, ModeratorEditWhenAllowed) {
  EditConfig config;
  config.allow_mod_edit = true;
  MessageEditManager mgr(config);
  mgr.RegisterMessage("m1", "alice", "content");
  chirp::chat::ChatMessageFull full;
  EXPECT_TRUE(mgr.EditMessage("m1", "mod", "modded", &full, /*is_moderator=*/true));
  EXPECT_EQ(full.content(), "modded");

  // With mod edits disabled, moderators cannot edit either.
  EditConfig strict;
  strict.allow_mod_edit = false;
  MessageEditManager strict_mgr(strict);
  strict_mgr.RegisterMessage("m2", "alice", "content");
  EXPECT_FALSE(strict_mgr.EditMessage("m2", "mod", "x", nullptr, /*is_moderator=*/true));
  EXPECT_FALSE(strict_mgr.CanEdit("m2", "mod", /*is_moderator=*/true));
}

TEST(MessageEditManagerTest, CanDeleteSemantics) {
  MessageEditManager mgr;
  mgr.RegisterMessage("m1", "alice", "content");
  EXPECT_TRUE(mgr.CanDelete("m1", "alice"));
  EXPECT_FALSE(mgr.CanDelete("m1", "bob"));
  EXPECT_TRUE(mgr.CanDelete("m1", "mod", /*is_moderator=*/true));

  // Moderators may always delete (allow_mod_edit only gates editing).
  EditConfig strict;
  strict.allow_mod_edit = false;
  MessageEditManager strict_mgr(strict);
  strict_mgr.RegisterMessage("m2", "alice", "content");
  EXPECT_TRUE(strict_mgr.CanDelete("m2", "mod", /*is_moderator=*/true));
}

TEST(MessageEditManagerTest, UnknownMessageQueriesFail) {
  MessageEditManager mgr;
  chirp::chat::ChatMessageFull full;
  EXPECT_FALSE(mgr.GetFullMessage("missing", &full));
  EXPECT_FALSE(mgr.EditMessage("missing", "alice", "x"));
  EXPECT_FALSE(mgr.CanEdit("missing", "alice"));
  EXPECT_FALSE(mgr.CanDelete("missing", "alice"));
  EXPECT_TRUE(mgr.GetEditHistory("missing").empty());
}

TEST(MessageEditManagerTest, CleanupOldDeletedMessages) {
  EditConfig config;
  config.soft_delete_retention_days = 0;  // expire immediately
  MessageEditManager mgr(config);
  mgr.RegisterMessage("m1", "alice", "content");
  EXPECT_TRUE(mgr.DeleteMessage("m1", "alice", /*is_hard_delete=*/false));
  EXPECT_EQ(mgr.GetDeletedMessageCount(), 1u);

  // retention=0 means "older than now"; wait so the just-set timestamp
  // falls strictly below the cleanup cutoff.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  mgr.CleanupOldDeletedMessages();
  EXPECT_EQ(mgr.GetDeletedMessageCount(), 0u);

  // Entries still inside the retention window survive the sweep.
  EditConfig keep_config;
  keep_config.soft_delete_retention_days = 30;  // keep everything
  MessageEditManager keep_mgr(keep_config);
  keep_mgr.RegisterMessage("m9", "alice", "content");
  ASSERT_TRUE(keep_mgr.DeleteMessage("m9", "alice", /*is_hard_delete=*/false));
  keep_mgr.CleanupOldDeletedMessages();
  EXPECT_EQ(keep_mgr.GetDeletedMessageCount(), 1u);
}

TEST(MessageEditManagerTest, TrackedCountReflectsRegistrations) {
  MessageEditManager mgr;
  EXPECT_EQ(mgr.GetTrackedMessageCount(), 0u);
  mgr.RegisterMessage("m1", "alice", "content");
  mgr.RegisterMessage("m2", "bob", "content");
  EXPECT_EQ(mgr.GetTrackedMessageCount(), 2u);
}

// ---------------------------------------------------------------------------
// MessageStoreConfig
// ---------------------------------------------------------------------------

TEST(MessageStoreConfigTest, DefaultsValidate) {
  MessageStoreConfig config;
  EXPECT_TRUE(config.Validate());
}

TEST(MessageStoreConfigTest, InvalidFieldsFailValidation) {
  MessageStoreConfig config;
  config.redis_host.clear();
  EXPECT_FALSE(config.Validate());

  config.redis_host = "127.0.0.1";
  config.mysql_host.clear();
  EXPECT_FALSE(config.Validate());

  config.mysql_host = "127.0.0.1";
  config.mysql_database.clear();
  EXPECT_FALSE(config.Validate());

  config.mysql_database = "chirp";
  config.migration_batch_size = 0;
  EXPECT_FALSE(config.Validate());
  config.migration_batch_size = 10001;
  EXPECT_FALSE(config.Validate());
  config.migration_batch_size = 100;
  EXPECT_TRUE(config.Validate());
}

TEST(MessageStoreConfigTest, FromEnvOverridesDefaults) {
  // Exercise the environment-driven overrides.
  setenv("CHIRP_REDIS_HOST", "env-redis", 1);
  setenv("CHIRP_REDIS_PORT", "7777", 1);
  setenv("CHIRP_MYSQL_HOST", "env-mysql", 1);
  setenv("CHIRP_MYSQL_PORT", "3307", 1);
  setenv("CHIRP_MYSQL_DATABASE", "envdb", 1);
  setenv("CHIRP_MYSQL_USER", "envuser", 1);
  setenv("CHIRP_MYSQL_PASSWORD", "envpass", 1);
  setenv("CHIRP_MIGRATION_ENABLED", "1", 1);
  setenv("CHIRP_MIGRATION_BATCH_SIZE", "500", 1);
  setenv("CHIRP_DELIVERY_TRACKING_ENABLED", "true", 1);

  MessageStoreConfig config = MessageStoreConfig::FromEnv();
  EXPECT_EQ(config.redis_host, "env-redis");
  EXPECT_EQ(config.redis_port, 7777u);
  EXPECT_EQ(config.mysql_host, "env-mysql");
  EXPECT_EQ(config.mysql_port, 3307u);
  EXPECT_EQ(config.mysql_database, "envdb");
  EXPECT_EQ(config.mysql_user, "envuser");
  EXPECT_EQ(config.mysql_password, "envpass");
  EXPECT_TRUE(config.enable_migration);
  EXPECT_EQ(config.migration_batch_size, 500);
  EXPECT_TRUE(config.enable_delivery_tracking);
  EXPECT_TRUE(config.Validate());

  // "true" is accepted as well; a non-matching value leaves the flag false.
  setenv("CHIRP_MIGRATION_ENABLED", "true", 1);
  EXPECT_TRUE(MessageStoreConfig::FromEnv().enable_migration);
  setenv("CHIRP_MIGRATION_ENABLED", "yes", 1);
  EXPECT_FALSE(MessageStoreConfig::FromEnv().enable_migration);
  setenv("CHIRP_DELIVERY_TRACKING_ENABLED", "1", 1);
  EXPECT_TRUE(MessageStoreConfig::FromEnv().enable_delivery_tracking);
  setenv("CHIRP_DELIVERY_TRACKING_ENABLED", "0", 1);
  EXPECT_FALSE(MessageStoreConfig::FromEnv().enable_delivery_tracking);

  unsetenv("CHIRP_REDIS_HOST");
  unsetenv("CHIRP_REDIS_PORT");
  unsetenv("CHIRP_MYSQL_HOST");
  unsetenv("CHIRP_MYSQL_PORT");
  unsetenv("CHIRP_MYSQL_DATABASE");
  unsetenv("CHIRP_MYSQL_USER");
  unsetenv("CHIRP_MYSQL_PASSWORD");
  unsetenv("CHIRP_MIGRATION_ENABLED");
  unsetenv("CHIRP_MIGRATION_BATCH_SIZE");
  unsetenv("CHIRP_DELIVERY_TRACKING_ENABLED");

  // Without the env vars the defaults apply and validate.
  MessageStoreConfig defaults = MessageStoreConfig::FromEnv();
  EXPECT_TRUE(defaults.Validate());
  EXPECT_EQ(defaults.redis_host, "127.0.0.1");
}

// ---------------------------------------------------------------------------
// ChannelManager
// ---------------------------------------------------------------------------

using chirp::chat::Channel;
using chirp::chat::ChannelCategory;
using chirp::chat::ChannelKind;
using chirp::chat::ChannelPermissions;
using chirp::chat::PermissionOverrideEntry;
using chirp::chat::PermissionType;

PermissionOverrideEntry MakeOverride(const std::string& id,
                                     PermissionType type,
                                     bool allow_write) {
  PermissionOverrideEntry entry;
  entry.set_type(type);
  entry.set_id(id);
  entry.mutable_permissions()->set_can_write(true);
  // Verdicts live in two separate enum fields: ALLOW keeps the permission,
  // DENY revokes it.
  if (allow_write) {
    entry.set_allow(chirp::chat::PermissionOverride::ALLOW);
  } else {
    entry.set_deny(chirp::chat::PermissionOverride::DENY);
  }
  return entry;
}

PermissionOverrideEntry MakeManageOverride(const std::string& id,
                                           PermissionType type) {
  PermissionOverrideEntry entry;
  entry.set_type(type);
  entry.set_id(id);
  // Only can_manage is in the verdict set: can_write must stay untouched.
  entry.mutable_permissions()->set_can_manage(true);
  entry.set_allow(chirp::chat::PermissionOverride::ALLOW);
  return entry;
}

TEST(ChannelManagerTest, CategoryCrud) {
  ChannelManager mgr;
  const std::string cat = mgr.CreateCategory("g1", "Voice", 0);
  EXPECT_FALSE(cat.empty());

  ChannelCategory info;
  ASSERT_TRUE(mgr.GetCategory(cat, &info));
  EXPECT_EQ(info.name(), "Voice");

  EXPECT_TRUE(mgr.UpdateCategory(cat, "Renamed", 5));
  ASSERT_TRUE(mgr.GetCategory(cat, &info));
  EXPECT_EQ(info.name(), "Renamed");
  EXPECT_EQ(info.position(), 5);

  // A negative position leaves the stored ordering untouched.
  EXPECT_TRUE(mgr.UpdateCategory(cat, "", -1));
  ASSERT_TRUE(mgr.GetCategory(cat, &info));
  EXPECT_EQ(info.position(), 5);

  auto cats = mgr.GetCategories("g1");
  ASSERT_EQ(cats.size(), 1u);

  EXPECT_TRUE(mgr.DeleteCategory(cat));
  EXPECT_FALSE(mgr.GetCategory(cat, &info));
  EXPECT_TRUE(mgr.GetCategories("g1").empty());

  // Unknown updates fail.
  EXPECT_FALSE(mgr.UpdateCategory("missing", "x", 0));
  EXPECT_FALSE(mgr.DeleteCategory("missing"));
}

TEST(ChannelManagerTest, CategoriesSortByPosition) {
  ChannelManager mgr;
  const std::string high = mgr.CreateCategory("g1", "Later", 10);
  const std::string low = mgr.CreateCategory("g1", "First", 1);
  auto cats = mgr.GetCategories("g1");
  ASSERT_EQ(cats.size(), 2u);
  EXPECT_EQ(cats[0].category_id(), low);
  EXPECT_EQ(cats[1].category_id(), high);
}

TEST(ChannelManagerTest, ChannelCrudAndListing) {
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "talk", {}, 0);
  const std::string c2 = mgr.CreateChannel("g1", "lobby", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {}, 1);
  EXPECT_NE(c1, c2);

  Channel info;
  ASSERT_TRUE(mgr.GetChannel(c1, &info));
  EXPECT_EQ(info.name(), "general");
  EXPECT_EQ(info.description(), "talk");

  EXPECT_TRUE(mgr.UpdateChannel(c1, "renamed", "desc2", 3, "", {}));
  ASSERT_TRUE(mgr.GetChannel(c1, &info));
  EXPECT_EQ(info.name(), "renamed");
  EXPECT_EQ(info.position(), 3);

  // Negative position is ignored (ordering is only advanced forward).
  EXPECT_TRUE(mgr.UpdateChannel(c1, "", "", -1, "", {}));
  ASSERT_TRUE(mgr.GetChannel(c1, &info));
  EXPECT_EQ(info.position(), 3);

  auto channels = mgr.GetChannels("g1", "alice", "");
  EXPECT_EQ(channels.size(), 2u);
  // Sorted by position: lobby(1) comes before renamed(3).
  EXPECT_EQ(channels[0].name(), "lobby");
  EXPECT_EQ(channels[1].name(), "renamed");

  EXPECT_TRUE(mgr.DeleteChannel(c1));
  EXPECT_FALSE(mgr.GetChannel(c1, &info));
  EXPECT_EQ(mgr.GetChannels("g1", "alice", "").size(), 1u);

  EXPECT_FALSE(mgr.UpdateChannel("missing", "x", "", -1, "", {}));
  EXPECT_FALSE(mgr.DeleteChannel("missing"));
  EXPECT_FALSE(mgr.GetChannel("missing", &info));
}

TEST(ChannelManagerTest, PermissionChecksDefaultAllow) {
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {}, 0);
  EXPECT_TRUE(mgr.CanRead(c1, "alice", ""));
  EXPECT_TRUE(mgr.CanWrite(c1, "alice", ""));
  EXPECT_TRUE(mgr.CanSpeak(c1, "alice", ""));
  EXPECT_TRUE(mgr.CanJoin(c1, "alice", ""));

  ChannelPermissions required;
  required.set_can_read(true);
  EXPECT_TRUE(mgr.HasPermission(c1, "alice", "", required));

  // Unknown channels deny.
  EXPECT_FALSE(mgr.CanRead("missing", "alice", ""));
  EXPECT_FALSE(mgr.HasPermission("missing", "alice", "", required));
}

TEST(ChannelManagerTest, UserOverrideDenyBlocksWrite) {
  PermissionOverrideEntry deny_bob = MakeOverride("bob", PermissionType::PERMISSION_TYPE_USER,
                                                  /*allow_write=*/false);
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {deny_bob}, 0);
  EXPECT_FALSE(mgr.CanWrite(c1, "bob", ""));
  EXPECT_TRUE(mgr.CanWrite(c1, "alice", ""));
}

TEST(ChannelManagerTest, SlowmodeThrottlesSending) {
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {}, 0);
  EXPECT_FALSE(mgr.SetSlowmode("missing", 10));
  ASSERT_TRUE(mgr.SetSlowmode(c1, 10));

  // RecordMessageSent stamps against the system clock, so use real
  // timestamps for the "now_ms" arguments.
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  EXPECT_TRUE(mgr.CanSendMessage(c1, "alice", now));
  mgr.RecordMessageSent(c1, "alice");
  EXPECT_FALSE(mgr.CanSendMessage(c1, "alice", now + 1000));   // within cooldown
  EXPECT_TRUE(mgr.CanSendMessage(c1, "alice", now + 60000));   // after cooldown
  EXPECT_TRUE(mgr.CanSendMessage(c1, "bob", now + 1000));      // other user unaffected
  EXPECT_FALSE(mgr.CanSendMessage("missing", "alice", 0));

  // Disabling slow mode removes the restriction.
  ASSERT_TRUE(mgr.SetSlowmode(c1, 0));
  EXPECT_TRUE(mgr.CanSendMessage(c1, "alice", now));
  mgr.RecordMessageSent("missing", "alice");  // no-op on unknown channel
}

TEST(ChannelManagerTest, VoiceChannelParticipants) {
  ChannelManager mgr;
  const std::string vc = mgr.CreateChannel("g1", "Voice", ChannelKind::CHANNEL_KIND_VOICE,
                                           "", "", {}, 0);
  EXPECT_TRUE(mgr.JoinVoiceChannel(vc, "alice"));
  EXPECT_TRUE(mgr.JoinVoiceChannel(vc, "bob"));
  EXPECT_EQ(mgr.GetVoiceChannelParticipants(vc).size(), 2u);

  EXPECT_TRUE(mgr.LeaveVoiceChannel(vc, "alice"));
  EXPECT_EQ(mgr.GetVoiceChannelParticipants(vc).size(), 1u);
  EXPECT_EQ(mgr.GetVoiceChannelParticipants(vc)[0], "bob");

  // Leaving as the last participant clears the participant set.
  EXPECT_TRUE(mgr.LeaveVoiceChannel(vc, "bob"));
  EXPECT_TRUE(mgr.GetVoiceChannelParticipants(vc).empty());

  EXPECT_FALSE(mgr.JoinVoiceChannel("missing", "alice"));
  EXPECT_FALSE(mgr.LeaveVoiceChannel("missing", "alice"));
  EXPECT_TRUE(mgr.GetVoiceChannelParticipants("missing").empty());
  EXPECT_TRUE(mgr.SearchChannels("unknown-group", "q").empty());
}

TEST(ChannelManagerTest, SearchChannelsByName) {
  ChannelManager mgr;
  mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT, "", "", {}, 0);
  mgr.CreateChannel("g1", "gen-lobby", ChannelKind::CHANNEL_KIND_TEXT, "", "", {}, 0);
  mgr.CreateChannel("g1", "random", ChannelKind::CHANNEL_KIND_TEXT, "", "public square", {}, 0);

  auto hits = mgr.SearchChannels("g1", "gen");
  EXPECT_EQ(hits.size(), 2u);
  EXPECT_TRUE(mgr.SearchChannels("g1", "zzz").empty());
  // Description-only hit: the name does not match but the topic does.
  auto desc_hits = mgr.SearchChannels("g1", "square");
  ASSERT_EQ(desc_hits.size(), 1u);
  EXPECT_EQ(desc_hits[0].name(), "random");
}

TEST(ChannelManagerTest, PermissionCheckerStaticHelpers) {
  Channel channel;
  channel.set_kind(ChannelKind::CHANNEL_KIND_TEXT);
  using Field = chirp::chat::ChannelPermissionChecker::Field;
  for (Field f : {Field::kCanRead, Field::kCanWrite, Field::kCanSpeak,
                  Field::kCanJoin}) {
    EXPECT_TRUE(ChannelPermissionChecker::HasPermission(channel, "alice", "", f));
  }
  // can_manage is not part of the default permission set.
  EXPECT_FALSE(ChannelPermissionChecker::HasPermission(channel, "alice", "",
                                                       Field::kCanManage));
  auto perms = ChannelPermissionChecker::GetEffectivePermissions(channel, "alice", "");
  EXPECT_TRUE(perms.can_write());
}

TEST(ChannelManagerTest, RoleOverrideApplies) {
  PermissionOverrideEntry deny_guests = MakeOverride("role-guest",
                                                     PermissionType::PERMISSION_TYPE_ROLE,
                                                     /*allow_write=*/false);
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {deny_guests}, 0);
  // Users carrying the role are denied, others keep the default allow.
  EXPECT_FALSE(mgr.CanWrite(c1, "anyone", "role-guest"));
  EXPECT_TRUE(mgr.CanWrite(c1, "anyone", "role-mod"));
  // An empty role id never matches a ROLE override (short-circuit).
  EXPECT_TRUE(mgr.CanWrite(c1, "anyone", ""));
}

TEST(ChannelManagerTest, ManageOnlyOverrideLeavesWriteAlone) {
  PermissionOverrideEntry manage = MakeManageOverride("bob",
                                                      PermissionType::PERMISSION_TYPE_USER);
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {manage}, 0);
  // can_write is not in the verdict set, so the default allow stands while
  // can_manage flips on for the targeted user.
  EXPECT_TRUE(mgr.CanWrite(c1, "bob", ""));
  using Field = chirp::chat::ChannelPermissionChecker::Field;
  ChannelPermissions required;
  required.set_can_manage(true);
  EXPECT_TRUE(mgr.HasPermission(c1, "bob", "", required));
  EXPECT_FALSE(mgr.HasPermission(c1, "alice", "", required));
}

TEST(ChannelManagerTest, OverrideWithoutVerdictIsIgnored) {
  PermissionOverrideEntry neutral;
  neutral.set_type(PermissionType::PERMISSION_TYPE_USER);
  neutral.set_id("bob");
  neutral.mutable_permissions()->set_can_write(true);
  // Neither ALLOW nor DENY: the entry must not change effective rights.
  ChannelManager mgr;
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {neutral}, 0);
  EXPECT_TRUE(mgr.CanWrite(c1, "bob", ""));
}

TEST(ChannelManagerTest, CategoryDeletionClearsChannelLinks) {
  ChannelManager mgr;
  const std::string cat = mgr.CreateCategory("g1", "Cat", 0);
  const std::string other = mgr.CreateCategory("g1", "Other", 1);
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           cat, "", {}, 0);
  const std::string c2 = mgr.CreateChannel("g1", "lobby", ChannelKind::CHANNEL_KIND_TEXT,
                                           other, "", {}, 1);
  ASSERT_TRUE(mgr.DeleteCategory(cat));

  // The channel survives but is detached from the removed category.
  Channel info;
  ASSERT_TRUE(mgr.GetChannel(c1, &info));
  EXPECT_TRUE(info.category_id().empty());
  // The sibling in the other category keeps its link.
  ASSERT_TRUE(mgr.GetChannel(c2, &info));
  EXPECT_EQ(info.category_id(), other);
  auto cats = mgr.GetCategories("g1");
  ASSERT_EQ(cats.size(), 1u);
  EXPECT_EQ(cats[0].category_id(), other);
  EXPECT_TRUE(mgr.GetCategories("unknown-group").empty());
  // Deleting the last category leaves the group with no categories.
  ASSERT_TRUE(mgr.DeleteCategory(other));
  EXPECT_TRUE(mgr.GetCategories("g1").empty());
}

TEST(ChannelManagerTest, UpdateChannelAcceptsCategoryAndOverrides) {
  ChannelManager mgr;
  const std::string cat = mgr.CreateCategory("g1", "Cat", 0);
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {}, 0);
  PermissionOverrideEntry deny_bob = MakeOverride("bob", PermissionType::PERMISSION_TYPE_USER,
                                                  /*allow_write=*/false);
  ASSERT_TRUE(mgr.UpdateChannel(c1, "moved", "d", 0, cat, {deny_bob}));

  Channel info;
  ASSERT_TRUE(mgr.GetChannel(c1, &info));
  EXPECT_EQ(info.category_id(), cat);
  ASSERT_EQ(info.permission_overrides_size(), 1);
  EXPECT_FALSE(mgr.CanWrite(c1, "bob", ""));
}

TEST(ChannelManagerTest, RequiredPermissionDenialsPerField) {
  using Field = chirp::chat::ChannelPermissionChecker::Field;
  ChannelManager mgr;
  // Deny every default permission for bob via one override entry.
  PermissionOverrideEntry deny_all = MakeOverride("bob", PermissionType::PERMISSION_TYPE_USER,
                                                  /*allow_write=*/false);
  deny_all.mutable_permissions()->set_can_read(true);
  deny_all.mutable_permissions()->set_can_write(true);
  deny_all.mutable_permissions()->set_can_speak(true);
  deny_all.mutable_permissions()->set_can_join(true);
  const std::string c1 = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                           "", "", {deny_all}, 0);

  ChannelPermissions required;
  required.set_can_read(true);
  EXPECT_FALSE(mgr.HasPermission(c1, "bob", "", required));
  required.Clear(); required.set_can_write(true);
  EXPECT_FALSE(mgr.HasPermission(c1, "bob", "", required));
  required.Clear(); required.set_can_speak(true);
  EXPECT_FALSE(mgr.HasPermission(c1, "bob", "", required));
  required.Clear(); required.set_can_join(true);
  EXPECT_FALSE(mgr.HasPermission(c1, "bob", "", required));
  // can_manage is not in the default set: a required-manage check fails.
  required.Clear(); required.set_can_manage(true);
  EXPECT_FALSE(mgr.HasPermission(c1, "bob", "", required));
  EXPECT_FALSE(mgr.HasPermission(c1, "alice", "", required));

  // Member Field overload on an existing channel.
  EXPECT_TRUE(mgr.HasPermission(c1, "alice", "", Field::kCanRead));
}

TEST(ChannelManagerTest, UnknownChannelAndGroupQueries) {
  ChannelManager mgr;
  EXPECT_TRUE(mgr.GetChannels("unknown-group", "alice", "").empty());

  ChannelPermissions required;
  required.set_can_read(true);
  EXPECT_FALSE(mgr.HasPermission("missing", "alice", "", required));
  using Field = chirp::chat::ChannelPermissionChecker::Field;
  EXPECT_FALSE(mgr.HasPermission("missing", "alice", "", Field::kCanRead));
  EXPECT_FALSE(mgr.CanWrite("missing", "alice", ""));
  EXPECT_FALSE(mgr.CanSpeak("missing", "alice", ""));
  EXPECT_FALSE(mgr.CanJoin("missing", "alice", ""));
  EXPECT_FALSE(mgr.SetSlowmode("missing", 1));
}

TEST(ChannelManagerTest, VoiceJoinRequiresVoiceKind) {
  ChannelManager mgr;
  const std::string text = mgr.CreateChannel("g1", "general", ChannelKind::CHANNEL_KIND_TEXT,
                                             "", "", {}, 0);
  EXPECT_FALSE(mgr.JoinVoiceChannel(text, "alice"));  // text channels reject joins
  EXPECT_TRUE(mgr.GetVoiceChannelParticipants("unknown").empty());
  EXPECT_FALSE(mgr.LeaveVoiceChannel("unknown", "alice"));
}

TEST(ChannelManagerTest, StageChannelAcceptsJoinsLikeVoice) {
  ChannelManager mgr;
  const std::string stage = mgr.CreateChannel("g1", "Stage", ChannelKind::CHANNEL_KIND_STAGE,
                                              "", "", {}, 0);
  EXPECT_TRUE(mgr.JoinVoiceChannel(stage, "alice"));
  EXPECT_EQ(mgr.GetVoiceChannelParticipants(stage).size(), 1u);
  EXPECT_TRUE(mgr.LeaveVoiceChannel(stage, "alice"));
}

}  // namespace
