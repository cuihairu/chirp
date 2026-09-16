// Unit tests for the social presence manager (PresenceManager). The manager
// is pure in-memory, so every branch is reachable directly; only the 24-hour
// offline-purge branch of CleanupOfflineUsers needs a time machine and is
// exercised as far as the public API allows.

#include "presence_manager.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using chirp::social::PresenceConfig;
using chirp::social::PresenceData;
using chirp::social::PresenceManager;
using chirp::social::PresenceStatus;
using chirp::social::UserActivity;

struct PresenceChange {
  std::string user_id;
  PresenceStatus old_status;
  PresenceStatus new_status;
};

class PresenceManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<PresenceManager>(PresenceConfig{});
  }

  std::unique_ptr<PresenceManager> manager_;
};

TEST(PresenceStatusConversion, RoundTripsEveryKnownStatus) {
  const std::pair<PresenceStatus, std::string> cases[] = {
      {PresenceStatus::OFFLINE, "offline"},
      {PresenceStatus::ONLINE, "online"},
      {PresenceStatus::IDLE, "idle"},
      {PresenceStatus::DO_NOT_DISTURB, "do_not_disturb"},
      {PresenceStatus::INVISIBLE, "invisible"},
      {PresenceStatus::IN_GAME, "in_game"},
      {PresenceStatus::IN_VOICE, "in_voice"},
      {PresenceStatus::IN_CALL, "in_call"},
  };
  for (const auto& [status, name] : cases) {
    EXPECT_EQ(PresenceManager::StatusToString(status), name);
    EXPECT_EQ(PresenceManager::StringToStatus(name), status);
  }
}

TEST(PresenceStatusConversion, UnknownValuesFallBackToOffline) {
  EXPECT_EQ(PresenceManager::StatusToString(static_cast<PresenceStatus>(99)), "offline");
  EXPECT_EQ(PresenceManager::StatusToString(static_cast<PresenceStatus>(-1)), "offline");
  EXPECT_EQ(PresenceManager::StringToStatus("bogus"), PresenceStatus::OFFLINE);
  EXPECT_EQ(PresenceManager::StringToStatus("ONLINE"), PresenceStatus::ONLINE);
}

TEST_F(PresenceManagerTest, UpdatePresenceCreatesOnlineUserAndNotifies) {
  std::vector<PresenceChange> changes;
  manager_->SetPresenceChangeCallback(
      [&](const std::string& uid, PresenceStatus old_s, PresenceStatus new_s) {
        changes.push_back({uid, old_s, new_s});
      });

  EXPECT_TRUE(manager_->UpdatePresence("alice", PresenceStatus::ONLINE, "dev1", "desktop"));

  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("alice", &data));
  EXPECT_EQ(data.status, PresenceStatus::ONLINE);
  EXPECT_EQ(data.device_id, "dev1");
  EXPECT_EQ(data.client_type, "desktop");
  EXPECT_GT(data.online_since, 0);
  EXPECT_GT(data.last_seen, 0);
  ASSERT_EQ(data.device_status.size(), 1u);
  EXPECT_EQ(data.device_status["dev1"], PresenceStatus::ONLINE);

  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].user_id, "alice");
  EXPECT_EQ(changes[0].old_status, PresenceStatus::OFFLINE);
  EXPECT_EQ(changes[0].new_status, PresenceStatus::ONLINE);
}

TEST_F(PresenceManagerTest, UpdatePresenceWithoutDeviceKeepsMapsSmall) {
  EXPECT_TRUE(manager_->UpdatePresence("bob", PresenceStatus::ONLINE));
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("bob", &data));
  EXPECT_TRUE(data.device_id.empty());
  EXPECT_TRUE(data.client_type.empty());
  EXPECT_TRUE(data.device_status.empty());
}

TEST_F(PresenceManagerTest, OverallStatusPrefersHighestPriorityDevice) {
  // Two devices: one ONLINE, one DO_NOT_DISTURB -> DND wins.
  manager_->UpdatePresence("carol", PresenceStatus::ONLINE, "phone");
  manager_->UpdatePresence("carol", PresenceStatus::DO_NOT_DISTURB, "desktop");

  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("carol", &data));
  EXPECT_EQ(data.status, PresenceStatus::DO_NOT_DISTURB);

  // IN_VOICE beats IN_GAME and ONLINE.
  manager_->UpdatePresence("dave", PresenceStatus::ONLINE, "phone");
  manager_->UpdatePresence("dave", PresenceStatus::IN_GAME, "pc");
  manager_->UpdatePresence("dave", PresenceStatus::IN_VOICE, "watch");
  ASSERT_TRUE(manager_->GetPresence("dave", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_VOICE);

  // IN_GAME beats ONLINE.
  manager_->UpdatePresence("erin", PresenceStatus::ONLINE, "phone");
  manager_->UpdatePresence("erin", PresenceStatus::IN_GAME, "pc");
  ASSERT_TRUE(manager_->GetPresence("erin", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_GAME);

  // IN_CALL is reported as IN_VOICE (they share the voice priority tier).
  manager_->UpdatePresence("frank", PresenceStatus::ONLINE, "phone");
  manager_->UpdatePresence("frank", PresenceStatus::IN_CALL, "pc");
  ASSERT_TRUE(manager_->GetPresence("frank", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_VOICE);

  // ONLINE beats IDLE.
  manager_->UpdatePresence("gina", PresenceStatus::IDLE, "phone");
  manager_->UpdatePresence("gina", PresenceStatus::ONLINE, "pc");
  ASSERT_TRUE(manager_->GetPresence("gina", &data));
  EXPECT_EQ(data.status, PresenceStatus::ONLINE);

  // Only IDLE devices -> IDLE.
  manager_->UpdatePresence("hank", PresenceStatus::IDLE, "phone");
  ASSERT_TRUE(manager_->GetPresence("hank", &data));
  EXPECT_EQ(data.status, PresenceStatus::IDLE);

  // Offline/invisible devices fall through to OFFLINE.
  manager_->UpdatePresence("iris", PresenceStatus::OFFLINE, "phone");
  manager_->UpdatePresence("iris", PresenceStatus::INVISIBLE, "pc");
  ASSERT_TRUE(manager_->GetPresence("iris", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);
}

TEST_F(PresenceManagerTest, RecordActivityRevivesIdleUsers) {
  manager_->UpdatePresence("john", PresenceStatus::IDLE, "phone");

  EXPECT_TRUE(manager_->RecordActivity("john", UserActivity::TYPING, "phone"));
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("john", &data));
  EXPECT_EQ(data.status, PresenceStatus::ONLINE);
  EXPECT_GT(data.online_since, 0);
  EXPECT_EQ(data.device_status["phone"], PresenceStatus::ONLINE);

  // Activity for an unknown user creates the record without going online
  // (status stays OFFLINE until an active activity arrives).
  EXPECT_TRUE(manager_->RecordActivity("kate", UserActivity::IDLE));
  ASSERT_TRUE(manager_->GetPresence("kate", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);

  EXPECT_TRUE(manager_->RecordActivity("kate", UserActivity::AWAY, "tablet"));
  ASSERT_TRUE(manager_->GetPresence("kate", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);
  EXPECT_EQ(data.device_id, "tablet");
}

TEST_F(PresenceManagerTest, RecordActivityOnOfflineUserGoesOnline) {
  EXPECT_TRUE(manager_->RecordActivity("luke", UserActivity::MOVING_MOUSE));
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("luke", &data));
  EXPECT_EQ(data.status, PresenceStatus::ONLINE);
  EXPECT_GT(data.online_since, 0);
}

TEST_F(PresenceManagerTest, CustomStatusSetClearAndMissing) {
  EXPECT_TRUE(manager_->SetCustomStatus("mary", "Shipping", ":ship:", 60000));
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("mary", &data));
  EXPECT_EQ(data.custom_status.text, "Shipping");
  EXPECT_EQ(data.custom_status.emoji, ":ship:");
  EXPECT_GT(data.custom_status.expires_at, 0);

  // Zero duration means no expiry.
  EXPECT_TRUE(manager_->SetCustomStatus("mary", "Idle", "", 0));
  ASSERT_TRUE(manager_->GetPresence("mary", &data));
  EXPECT_EQ(data.custom_status.expires_at, 0);

  EXPECT_TRUE(manager_->ClearCustomStatus("mary"));
  ASSERT_TRUE(manager_->GetPresence("mary", &data));
  EXPECT_EQ(data.custom_status.text, "");

  // Clearing an unknown user fails.
  EXPECT_FALSE(manager_->ClearCustomStatus("nobody"));
}

TEST_F(PresenceManagerTest, SetActivityMapsKnownActivityTypes) {
  // Known activity types map to their status; unknown ones leave it alone.
  manager_->SetActivity("nick", "game", "chess");
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("nick", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_GAME);

  manager_->SetActivity("nick", "voice", "call with dad");
  ASSERT_TRUE(manager_->GetPresence("nick", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_VOICE);

  manager_->SetActivity("nick", "call", "dialing");
  ASSERT_TRUE(manager_->GetPresence("nick", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_CALL);

  // Unmapped activity keeps the previous status (still IN_CALL here).
  manager_->SetActivity("nick", "music", "song");
  ASSERT_TRUE(manager_->GetPresence("nick", &data));
  EXPECT_EQ(data.status, PresenceStatus::IN_CALL);
  EXPECT_EQ(data.activity_type, "music");
  EXPECT_EQ(data.activity_details, "song");

  // On a fresh user an unmapped activity leaves the status offline.
  manager_->SetActivity("nova", "music", "");
  ASSERT_TRUE(manager_->GetPresence("nova", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);
}

TEST_F(PresenceManagerTest, GetPresenceRejectsNullAndUnknownUsers) {
  EXPECT_FALSE(manager_->GetPresence("alice", nullptr));

  PresenceData data;
  data.status = PresenceStatus::ONLINE;
  ASSERT_TRUE(manager_->GetPresence("ghost", &data));
  EXPECT_EQ(data.user_id, "ghost");
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);
  EXPECT_EQ(data.last_seen, 0);
}

TEST_F(PresenceManagerTest, GetPresenceBatchMixesKnownAndUnknown) {
  manager_->UpdatePresence("alice", PresenceStatus::ONLINE);
  auto batch = manager_->GetPresenceBatch({"alice", "ghost"});
  ASSERT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch["alice"].status, PresenceStatus::ONLINE);
  EXPECT_EQ(batch["ghost"].status, PresenceStatus::OFFLINE);
}

TEST_F(PresenceManagerTest, GetOnlineFriendsFiltersByStatusAndRecency) {
  PresenceConfig config;
  config.offline_timeout_ms = 600000;
  chirp::social::PresenceManager mgr(config);

  mgr.UpdatePresence("alice", PresenceStatus::ONLINE);
  mgr.UpdatePresence("bob", PresenceStatus::IN_GAME);
  mgr.UpdatePresence("carol", PresenceStatus::OFFLINE);
  mgr.UpdatePresence("dave", PresenceStatus::INVISIBLE);
  mgr.UpdatePresence("idle", PresenceStatus::IDLE);
  mgr.UpdatePresence("gone", PresenceStatus::ONLINE);

  auto friends = mgr.GetOnlineFriends(
      "me", {"alice", "bob", "carol", "dave", "idle", "missing"});
  std::sort(friends.begin(), friends.end());
  EXPECT_EQ(friends, (std::vector<std::string>{"alice", "bob", "idle"}));
}

TEST_F(PresenceManagerTest, SerializePresenceEmitsFields) {
  PresenceData data;
  data.user_id = "alice";
  data.status = PresenceStatus::ONLINE;
  data.status_message = "hello";
  data.activity_type = "game";
  data.activity_details = "chess";
  data.last_seen = 123;
  data.online_since = 100;
  data.custom_status.text = "hi";
  data.custom_status.emoji = ":)";

  const std::string json = manager_->SerializePresence(data);
  EXPECT_NE(json.find("\"user_id\":\"alice\""), std::string::npos);
  EXPECT_NE(json.find("\"status\":\"online\""), std::string::npos);
  EXPECT_NE(json.find("\"last_seen\":123"), std::string::npos);
  EXPECT_NE(json.find("\"online_since\":100"), std::string::npos);
  EXPECT_NE(json.find("\"text\":\"hi\""), std::string::npos);

  // Deserialization is intentionally unimplemented.
  PresenceData out;
  EXPECT_FALSE(manager_->DeserializePresence(json, &out));
}

TEST_F(PresenceManagerTest, SessionRegisterUnregisterAndCounts) {
  EXPECT_TRUE(manager_->RegisterSession("oscar", "s1", "phone"));
  EXPECT_TRUE(manager_->RegisterSession("oscar", "s2", "pc"));

  auto sessions = manager_->GetUserSessions("oscar");
  std::sort(sessions.begin(), sessions.end());
  EXPECT_EQ(sessions, (std::vector<std::string>{"s1", "s2"}));
  EXPECT_TRUE(manager_->GetUserSessions("ghost").empty());

  EXPECT_EQ(manager_->GetTotalSessionCount(), 2u);
  EXPECT_EQ(manager_->GetOnlineUserCount(), 1u);

  EXPECT_TRUE(manager_->UnregisterSession("oscar", "s1"));
  EXPECT_FALSE(manager_->UnregisterSession("ghost", "s9"));
  EXPECT_EQ(manager_->GetTotalSessionCount(), 1u);

  // Removing the last session flips the user offline.
  EXPECT_TRUE(manager_->UnregisterSession("oscar", "s2"));
  PresenceData data;
  ASSERT_TRUE(manager_->GetPresence("oscar", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);
}

TEST_F(PresenceManagerTest, CleanupIdleUsersMarksIdleThenOffline) {
  // Negative timeouts put the cutoffs reliably past last_seen (a zero
  // timeout can tie within the same millisecond).
  PresenceConfig config;
  config.idle_timeout_ms = -1;
  config.offline_timeout_ms = -1;
  chirp::social::PresenceManager fast_offline(config);
  fast_offline.UpdatePresence("pete", PresenceStatus::ONLINE);
  fast_offline.CleanupIdleUsers();
  PresenceData data;
  ASSERT_TRUE(fast_offline.GetPresence("pete", &data));
  EXPECT_EQ(data.status, PresenceStatus::OFFLINE);

  // Idle cutoff crossed, offline cutoff far away -> only idle.
  PresenceConfig idle_only;
  idle_only.idle_timeout_ms = -1;
  idle_only.offline_timeout_ms = 100000000;
  chirp::social::PresenceManager idle(idle_only);
  idle.UpdatePresence("quinn", PresenceStatus::ONLINE);
  idle.CleanupIdleUsers();
  ASSERT_TRUE(idle.GetPresence("quinn", &data));
  EXPECT_EQ(data.status, PresenceStatus::IDLE);
}

TEST_F(PresenceManagerTest, CleanupIdleUsersNotifiesOnChange) {
  std::atomic<int> notifications{0};
  PresenceConfig config;
  config.idle_timeout_ms = -1;
  config.offline_timeout_ms = -1;
  chirp::social::PresenceManager mgr(config);
  mgr.SetPresenceChangeCallback(
      [&](const std::string&, PresenceStatus, PresenceStatus) { ++notifications; });

  mgr.UpdatePresence("rita", PresenceStatus::ONLINE);  // OFFLINE -> ONLINE: +1
  mgr.CleanupIdleUsers();                              // ONLINE -> OFFLINE: +1
  mgr.CleanupIdleUsers();                              // already OFFLINE: no change
  EXPECT_EQ(notifications.load(), 2);
}

TEST_F(PresenceManagerTest, CleanupOfflineUsersKeepsRecentUsers) {
  manager_->UpdatePresence("sam", PresenceStatus::OFFLINE);
  manager_->UpdatePresence("tina", PresenceStatus::ONLINE);
  manager_->CleanupOfflineUsers();

  PresenceData data;
  EXPECT_TRUE(manager_->GetPresence("sam", &data));   // not old enough to purge
  EXPECT_TRUE(manager_->GetPresence("tina", &data));  // not offline at all
}

TEST_F(PresenceManagerTest, OnlineUserCountIgnoresInvisible) {
  PresenceManager mgr(PresenceConfig{});
  mgr.UpdatePresence("vic", PresenceStatus::INVISIBLE);
  EXPECT_EQ(mgr.GetOnlineUserCount(), 0u);
}

} // namespace
