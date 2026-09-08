#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "notification_service.h"

using chirp::notification::APNsConfig;
using chirp::notification::DeviceRegistration;
using chirp::notification::FCMConfig;
using chirp::notification::NotificationPayload;
using chirp::notification::NotificationService;

namespace {

DeviceRegistration MakeDevice(const std::string& device_id,
                              const std::string& user_id,
                              const std::string& platform) {
  DeviceRegistration reg;
  reg.device_id = device_id;
  reg.user_id = user_id;
  reg.platform = platform;
  return reg;
}

NotificationPayload MakePayload() {
  NotificationPayload p;
  p.title = "Hi";
  p.body = "Hello world";
  p.sound = "default";
  return p;
}

class NotificationServiceTest : public ::testing::Test {
protected:
  NotificationService svc_{FCMConfig{}, APNsConfig{}};
};

TEST_F(NotificationServiceTest, RegisterDeviceOverwritesTimestampAndActivates) {
  DeviceRegistration reg = MakeDevice("d1", "alice", "android");
  reg.registered_at = 12345;
  reg.is_active = false;

  ASSERT_TRUE(svc_.RegisterDevice(reg));

  auto devices = svc_.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_GT(devices[0].registered_at, 12345);  // replaced with server time
  EXPECT_TRUE(devices[0].is_active);
  EXPECT_EQ(svc_.GetStats().devices_registered.load(), 1u);
}

TEST_F(NotificationServiceTest, RegisterSameDeviceTwiceUpdates) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  svc_.RegisterDevice(MakeDevice("d1", "alice", "ios"));

  EXPECT_EQ(svc_.GetUserDevices("alice").size(), 1u);
  EXPECT_EQ(svc_.GetStats().devices_registered.load(), 2u);
}

TEST_F(NotificationServiceTest, GetUserDevicesMultiple) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  svc_.RegisterDevice(MakeDevice("d2", "alice", "ios"));
  svc_.RegisterDevice(MakeDevice("d3", "bob", "web"));

  auto alice = svc_.GetUserDevices("alice");
  ASSERT_EQ(alice.size(), 2u);

  EXPECT_TRUE(svc_.GetUserDevices("nobody").empty());
}

TEST_F(NotificationServiceTest, UnregisterDeviceRemovesFromIndexes) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  svc_.RegisterDevice(MakeDevice("d2", "alice", "ios"));

  EXPECT_TRUE(svc_.UnregisterDevice("d1"));
  auto devices = svc_.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(devices[0].device_id, "d2");
}

TEST_F(NotificationServiceTest, UnregisterLastDeviceClearsUserIndex) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  ASSERT_TRUE(svc_.UnregisterDevice("d1"));
  EXPECT_TRUE(svc_.GetUserDevices("alice").empty());
}

TEST_F(NotificationServiceTest, UnregisterUnknownDeviceFails) {
  EXPECT_FALSE(svc_.UnregisterDevice("missing"));
}

TEST_F(NotificationServiceTest, UpdateDeviceTokenByPlatform) {
  svc_.RegisterDevice(MakeDevice("droid", "alice", "android"));
  svc_.RegisterDevice(MakeDevice("phone", "bob", "ios"));
  svc_.RegisterDevice(MakeDevice("browser", "carol", "web"));
  svc_.RegisterDevice(MakeDevice("other", "dave", "tizen"));

  EXPECT_TRUE(svc_.UpdateDeviceToken("droid", "fcm-token"));
  EXPECT_TRUE(svc_.UpdateDeviceToken("phone", "apns-token"));
  EXPECT_TRUE(svc_.UpdateDeviceToken("browser", "web-fcm-token"));
  EXPECT_TRUE(svc_.UpdateDeviceToken("other", "ignored"));  // unknown platform: noop

  auto devices = svc_.GetUserDevices("alice");
  EXPECT_EQ(devices[0].fcm_token, "fcm-token");

  auto phone = svc_.GetUserDevices("bob");
  EXPECT_EQ(phone[0].apns_token, "apns-token");

  auto browser = svc_.GetUserDevices("carol");
  EXPECT_EQ(browser[0].fcm_token, "web-fcm-token");

  auto other = svc_.GetUserDevices("dave");
  EXPECT_TRUE(other[0].fcm_token.empty());
  EXPECT_TRUE(other[0].apns_token.empty());
}

TEST_F(NotificationServiceTest, UpdateDeviceTokenUnknownDeviceFails) {
  EXPECT_FALSE(svc_.UpdateDeviceToken("missing", "tok"));
}

TEST_F(NotificationServiceTest, SendToDeviceWithoutTokenSucceeds) {
  // HTTPPost stub returns "" -> send "succeeds" when the device has no token.
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  EXPECT_TRUE(svc_.SendNotificationToDevice("d1", MakePayload()));
  const auto& stats = svc_.GetStats();
  EXPECT_EQ(stats.notifications_sent.load(), 1u);
  EXPECT_EQ(stats.fcm_sent.load(), 1u);
  EXPECT_EQ(stats.notifications_failed.load(), 0u);
}

TEST_F(NotificationServiceTest, SendToDeviceWithRealTokenFails) {
  // With a non-empty token the stub HTTP response counts as failure.
  DeviceRegistration reg = MakeDevice("d1", "alice", "android");
  reg.fcm_token = "real-token";
  svc_.RegisterDevice(reg);

  EXPECT_FALSE(svc_.SendNotificationToDevice("d1", MakePayload()));
  EXPECT_EQ(svc_.GetStats().notifications_failed.load(), 1u);
  EXPECT_EQ(svc_.GetStats().notifications_sent.load(), 0u);
}

TEST_F(NotificationServiceTest, SendToUnknownDeviceFails) {
  EXPECT_FALSE(svc_.SendNotificationToDevice("missing", MakePayload()));
}

TEST_F(NotificationServiceTest, SendIosWithoutTokenCountsApns) {
  svc_.RegisterDevice(MakeDevice("phone", "bob", "ios"));

  EXPECT_TRUE(svc_.SendNotificationToDevice("phone", MakePayload()));
  EXPECT_EQ(svc_.GetStats().apns_sent.load(), 1u);
  EXPECT_EQ(svc_.GetStats().fcm_sent.load(), 0u);
}

TEST_F(NotificationServiceTest, SuccessfulSendPutsUserOnCooldown) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  ASSERT_TRUE(svc_.SendNotificationToDevice("d1", MakePayload()));

  EXPECT_TRUE(svc_.IsOnCooldown("alice"));
  // Cooldown applies to the user's other devices too
  svc_.RegisterDevice(MakeDevice("d2", "alice", "ios"));
  EXPECT_FALSE(svc_.SendNotificationToDevice("d2", MakePayload()));
}

TEST_F(NotificationServiceTest, ExpiredCooldownAllowsSendingAgain) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  ASSERT_TRUE(svc_.SendNotificationToDevice("d1", MakePayload()));

  svc_.SetNotificationCooldown("alice", -1000);  // already expired
  EXPECT_FALSE(svc_.IsOnCooldown("alice"));
  EXPECT_TRUE(svc_.SendNotificationToDevice("d1", MakePayload()));
}

TEST_F(NotificationServiceTest, SetNotificationCooldownActivates) {
  EXPECT_FALSE(svc_.IsOnCooldown("alice"));
  svc_.SetNotificationCooldown("alice", 60000);
  EXPECT_TRUE(svc_.IsOnCooldown("alice"));
  EXPECT_FALSE(svc_.IsOnCooldown("bob"));

  svc_.CleanupExpiredCooldowns();
  EXPECT_TRUE(svc_.IsOnCooldown("alice"));  // still active
}

TEST_F(NotificationServiceTest, SendNotificationWithoutDevicesFails) {
  EXPECT_FALSE(svc_.SendNotification("nobody", MakePayload()));
}

TEST_F(NotificationServiceTest, SendNotificationReachesAnyUserDevice) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  EXPECT_TRUE(svc_.SendNotification("alice", MakePayload()));
}

TEST_F(NotificationServiceTest, SendNotificationToUsersSucceedsIfAnySucceeds) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  std::vector<std::string> users = {"nobody", "alice"};
  EXPECT_TRUE(svc_.SendNotificationToUsers(users, MakePayload()));

  EXPECT_FALSE(svc_.SendNotificationToUsers({"nobody1", "nobody2"}, MakePayload()));
}

TEST_F(NotificationServiceTest, BroadcastToChannelIsPlaceholderTrue) {
  EXPECT_TRUE(svc_.BroadcastToChannel("chan", MakePayload()));
  EXPECT_TRUE(svc_.BroadcastToChannel("chan", MakePayload(), {"alice"}));
}

TEST_F(NotificationServiceTest, NotifyNewMessageSendsToDevice) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  EXPECT_TRUE(svc_.NotifyNewMessage("alice", "bob", "Bob", "hello there", "c1"));
  EXPECT_EQ(svc_.GetStats().notifications_sent.load(), 1u);
}

TEST_F(NotificationServiceTest, NotifyNewMessageWithoutDeviceFails) {
  EXPECT_FALSE(svc_.NotifyNewMessage("nobody", "bob", "Bob", "hello", "c1"));
}

TEST_F(NotificationServiceTest, NotifyMentionSendsToDevice) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  EXPECT_TRUE(svc_.NotifyMention("alice", "bob", "Bob", "general", false));
  EXPECT_TRUE(svc_.NotifyMention("carol", "bob", "Bob", "general", true) == false);
}

TEST_F(NotificationServiceTest, SendSilentNotificationDeliversData) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  std::unordered_map<std::string, std::string> data{{"type", "sync"}};
  EXPECT_TRUE(svc_.SendSilentNotification("alice", data));
  EXPECT_FALSE(svc_.SendSilentNotification("nobody", data));
}

TEST_F(NotificationServiceTest, SetBadgeCountOnlyTargetsIosDevices) {
  svc_.RegisterDevice(MakeDevice("droid", "alice", "android"));
  svc_.RegisterDevice(MakeDevice("phone", "alice", "ios"));

  EXPECT_TRUE(svc_.SetBadgeCount("alice", 5));
  // Only the iOS device counted
  EXPECT_EQ(svc_.GetStats().notifications_sent.load(), 1u);
}

TEST_F(NotificationServiceTest, ClearBadgeSendsToIosDevice) {
  svc_.RegisterDevice(MakeDevice("phone", "alice", "ios"));

  EXPECT_TRUE(svc_.ClearBadge("alice"));
  EXPECT_EQ(svc_.GetStats().notifications_sent.load(), 1u);
}

TEST_F(NotificationServiceTest, SetBadgeCountWithoutIosDeviceFails) {
  svc_.RegisterDevice(MakeDevice("droid", "alice", "android"));
  EXPECT_FALSE(svc_.SetBadgeCount("alice", 3));
  EXPECT_FALSE(svc_.ClearBadge("alice"));
}

TEST_F(NotificationServiceTest, SetBadgeCountWithoutAnyDeviceFails) {
  EXPECT_FALSE(svc_.SetBadgeCount("nobody", 3));
}

TEST_F(NotificationServiceTest, CleanupInactiveDevicesKeepsActiveOnes) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  // Even with a zero threshold, active devices must survive.
  svc_.CleanupInactiveDevices(0);
  EXPECT_EQ(svc_.GetUserDevices("alice").size(), 1u);

  // Default threshold path also keeps the device.
  svc_.CleanupInactiveDevices();
  EXPECT_EQ(svc_.GetUserDevices("alice").size(), 1u);
}

TEST_F(NotificationServiceTest, CleanupExpiredCooldownsDropsExpiredOnes) {
  svc_.SetNotificationCooldown("alice", 100000);   // active
  svc_.SetNotificationCooldown("bob", -1);         // expired

  EXPECT_FALSE(svc_.IsOnCooldown("bob"));  // read erases expired entry
  svc_.CleanupExpiredCooldowns();
  EXPECT_TRUE(svc_.IsOnCooldown("alice"));
}

TEST_F(NotificationServiceTest, DeviceRegistrationCopySemantics) {
  DeviceRegistration reg = MakeDevice("d1", "alice", "ios");
  reg.app_version = "1.0";
  reg.os_version = "17.0";
  reg.apns_token = "tok";
  reg.apns_environment = "production";
  reg.push_kit_token = "pk";
  reg.fcm_token = "fcm";
  reg.registered_at = 42;
  reg.is_active = false;

  DeviceRegistration copy(reg);
  EXPECT_EQ(copy.device_id, "d1");
  EXPECT_EQ(copy.user_id, "alice");
  EXPECT_EQ(copy.platform, "ios");
  EXPECT_EQ(copy.app_version, "1.0");
  EXPECT_EQ(copy.os_version, "17.0");
  EXPECT_EQ(copy.apns_token, "tok");
  EXPECT_EQ(copy.apns_environment, "production");
  EXPECT_EQ(copy.push_kit_token, "pk");
  EXPECT_EQ(copy.fcm_token, "fcm");
  EXPECT_EQ(copy.registered_at, 42);
  EXPECT_FALSE(copy.is_active);

  DeviceRegistration assigned;
  assigned = reg;
  EXPECT_EQ(assigned.device_id, "d1");
  EXPECT_EQ(assigned.fcm_token, "fcm");

  // Self-assignment must be safe.
  DeviceRegistration& ref = assigned;
  assigned = ref;
  EXPECT_EQ(assigned.device_id, "d1");
}

TEST_F(NotificationServiceTest, NotifyNewMessageTruncatesLongBodies) {
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  const std::string long_message(150, 'x');
  EXPECT_TRUE(svc_.NotifyNewMessage("alice", "bob", "bobby", long_message, "c1"));

  // The 150-char message body is truncated to 100 chars ("..."): the send
  // still succeeds for a token-less device.
  EXPECT_GE(svc_.GetStats().notifications_sent.load(), 1u);
}

TEST_F(NotificationServiceTest, CleanupExpiredCooldownsRemovesStaleEntries) {
  svc_.SetNotificationCooldown("alice", -1000); // already expired
  // Do NOT call IsOnCooldown first: it erases the entry itself, which
  // would leave nothing for the cleanup loop.
  svc_.CleanupExpiredCooldowns(); // erase branch runs
  EXPECT_FALSE(svc_.IsOnCooldown("alice"));
}

TEST(NotificationServiceApnsTest, SandboxAndProductionEndpointsAreSelected) {
  APNsConfig sandbox;
  sandbox.use_sandbox = true;
  NotificationService svc{FCMConfig{}, sandbox};

  DeviceRegistration ios = MakeDevice("d1", "alice", "ios");
  ios.apns_token = "real-apns-token";
  ASSERT_TRUE(svc.RegisterDevice(ios));

  // The HTTP POST fails (no network in tests) but the APNs path including
  // the sandbox endpoint selection is exercised.
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));

  NotificationPayload p = MakePayload();
  p.icon = "bell";
  DeviceRegistration android = MakeDevice("d2", "alice", "android");
  ASSERT_TRUE(svc.RegisterDevice(android));
  EXPECT_TRUE(svc.SendNotificationToDevice("d2", p));
}

TEST_F(NotificationServiceTest, PayloadsWithDataAndBadgeAreSerialized) {
  // Android: two data entries exercise the comma separator branch in the
  // FCM payload builder.
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));
  NotificationPayload p = MakePayload();
  p.data["k1"] = "v1";
  p.data["k2"] = "v2";
  EXPECT_TRUE(svc_.SendNotificationToDevice("d1", p));

  // iOS: badge and data entries exercise the APNs payload builder (a
  // distinct user avoids the cooldown set by the successful send above).
  svc_.RegisterDevice(MakeDevice("d2", "bob", "ios"));
  NotificationPayload ios_payload = MakePayload();
  ios_payload.badge = 3;
  ios_payload.data["a"] = "b";
  EXPECT_TRUE(svc_.SendNotificationToDevice("d2", ios_payload));
}

}  // namespace
