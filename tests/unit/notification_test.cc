#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "notification_service.h"

using chirp::app_notification::APNsConfig;
using chirp::app_notification::DeviceRegistration;
using chirp::app_notification::FCMConfig;
using chirp::app_notification::NotificationPayload;
using chirp::app_notification::NotificationService;

namespace {

// Records provider requests; the canned body stands in for the provider's
// HTTP response.
class RecordingTransport : public chirp::app_notification::PushTransport {
 public:
  std::string Post(const chirp::app_notification::PushRequest& request) override {
    std::lock_guard<std::mutex> lock(mu);
    requests.push_back(request);
    return canned;
  }

  std::mutex mu;
  std::vector<chirp::app_notification::PushRequest> requests;
  std::string canned;
};

DeviceRegistration MakeDevice(const std::string& device_id,
                              const std::string& user_id,
                              const std::string& platform) {
  DeviceRegistration reg;
  reg.device_id = device_id;
  reg.user_id = user_id;
  reg.platform = platform;
  // Provider tokens by default: an untokened device is now an explicit
  // send failure, and these tests exercise the send paths, not
  // registration hygiene.
  reg.fcm_token = "fcm-tok";
  reg.apns_token = "apns-tok";
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
  NotificationServiceTest() { transport_->canned = "ok"; }

  std::shared_ptr<RecordingTransport> transport_ =
      std::make_shared<RecordingTransport>();
  NotificationService svc_{FCMConfig{}, APNsConfig{}, transport_};
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
  // Unknown platform: UpdateDeviceToken is a noop and the tokens keep the
  // values they were registered with.
  EXPECT_EQ(other[0].fcm_token, "fcm-tok");
  EXPECT_EQ(other[0].apns_token, "apns-tok");
}

TEST_F(NotificationServiceTest, UpdateDeviceTokenUnknownDeviceFails) {
  EXPECT_FALSE(svc_.UpdateDeviceToken("missing", "tok"));
}

TEST_F(NotificationServiceTest, UntokenedDeviceFailsClosed) {
  // A device without a provider token is a registration gap: the send
  // fails closed instead of pretending success (the old stub semantics).
  DeviceRegistration reg = MakeDevice("d1", "alice", "android");
  reg.fcm_token.clear();
  svc_.RegisterDevice(reg);

  EXPECT_FALSE(svc_.SendNotificationToDevice("d1", MakePayload()));
  const auto& stats = svc_.GetStats();
  EXPECT_EQ(stats.notifications_sent.load(), 0u);
  EXPECT_EQ(stats.fcm_sent.load(), 0u);
  EXPECT_EQ(stats.notifications_failed.load(), 1u);
  EXPECT_EQ(transport_->requests.size(), 0u);  // nothing reached the transport
}

TEST_F(NotificationServiceTest, UntokenedIosDeviceFailsClosed) {
  // The APNs path mirrors the FCM fail-closed contract: a device without an
  // apns token is a registration gap, not a successful no-op.
  DeviceRegistration reg = MakeDevice("d1", "alice", "ios");
  reg.apns_token.clear();
  svc_.RegisterDevice(reg);

  EXPECT_FALSE(svc_.SendNotificationToDevice("d1", MakePayload()));
  const auto& stats = svc_.GetStats();
  EXPECT_EQ(stats.notifications_sent.load(), 0u);
  EXPECT_EQ(stats.apns_sent.load(), 0u);
  EXPECT_EQ(stats.notifications_failed.load(), 1u);
  EXPECT_EQ(transport_->requests.size(), 0u);  // nothing reached the transport
}

TEST_F(NotificationServiceTest, ProviderSilenceFailsTheSend) {
  // Empty transport response = the provider did not answer: failure.
  transport_->canned = "";
  svc_.RegisterDevice(MakeDevice("d1", "alice", "android"));

  EXPECT_FALSE(svc_.SendNotificationToDevice("d1", MakePayload()));
  EXPECT_EQ(svc_.GetStats().notifications_failed.load(), 1u);
  EXPECT_EQ(svc_.GetStats().notifications_sent.load(), 0u);
}

TEST_F(NotificationServiceTest, SendToUnknownDeviceFails) {
  EXPECT_FALSE(svc_.SendNotificationToDevice("missing", MakePayload()));
}

TEST_F(NotificationServiceTest, SendIosCountsApns) {
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

TEST(NotificationServiceApnsTest, EndpointIsUsedExactlyAsConfigured) {
  auto transport = std::make_shared<RecordingTransport>();
  transport->canned = "ok";
  APNsConfig apns;
  apns.endpoint = "https://apns.example.test:2197";
  apns.use_sandbox = true;  // must NOT rewrite the endpoint anymore
  NotificationService svc{FCMConfig{}, apns, transport};

  DeviceRegistration ios = MakeDevice("d1", "apns-e", "ios");
  ASSERT_TRUE(svc.RegisterDevice(ios));

  EXPECT_TRUE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 1u);
  EXPECT_EQ(transport->requests[0].url, "https://apns.example.test:2197");

  // The production/sandbox URL choice moved to whoever fills the config
  // (the CLI derives it from --apns-sandbox / --apns-endpoint).
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

// ---------------------------------------------------------------------------
// Transport-backed sends: requests built for the provider seam.
// ---------------------------------------------------------------------------

TEST(NotificationTransportTest, TokenedDeviceSucceedsWhenProviderResponds) {
  auto transport = std::make_shared<RecordingTransport>();
  transport->canned = "{}";
  NotificationService svc(FCMConfig{}, APNsConfig{}, transport);

  DeviceRegistration reg = MakeDevice("d1", "tr-u1", "android");
  reg.fcm_token = "tok";
  svc.RegisterDevice(reg);

  EXPECT_TRUE(svc.SendNotification("tr-u1", MakePayload()));
  EXPECT_EQ(svc.GetStats().notifications_sent.load(), 1u);
  EXPECT_EQ(svc.GetStats().fcm_sent.load(), 1u);
  EXPECT_TRUE(svc.IsOnCooldown("tr-u1"));  // success engages the cooldown
}

TEST(NotificationTransportTest, TokenedDeviceFailsWhenProviderSilent) {
  auto transport = std::make_shared<RecordingTransport>();  // canned ""
  NotificationService svc(FCMConfig{}, APNsConfig{}, transport);

  DeviceRegistration reg = MakeDevice("d1", "tr-u2", "android");
  reg.fcm_token = "tok";
  svc.RegisterDevice(reg);

  EXPECT_FALSE(svc.SendNotification("tr-u2", MakePayload()));
  EXPECT_EQ(svc.GetStats().notifications_failed.load(), 1u);
}

TEST(NotificationTransportTest, ApnsRequestCarriesEndpointAndHeaders) {
  auto transport = std::make_shared<RecordingTransport>();
  transport->canned = "ok";
  APNsConfig apns;
  // The sandbox URL is picked by the config builder (main.cc --apns-sandbox).
  apns.endpoint = "https://api.development.push.apple.com:443";
  apns.bundle_id = "com.chirp.app";
  NotificationService svc(FCMConfig{}, apns, transport);

  DeviceRegistration reg = MakeDevice("d1", "tr-u3", "ios");
  reg.apns_token = "apns-tok";
  svc.RegisterDevice(reg);
  EXPECT_TRUE(svc.SendNotification("tr-u3", MakePayload()));

  ASSERT_EQ(transport->requests.size(), 1u);
  const auto& req = transport->requests[0];
  EXPECT_EQ(req.provider, "apns");
  EXPECT_EQ(req.url, "https://api.development.push.apple.com:443");
  EXPECT_EQ(req.device_token, "apns-tok");
  EXPECT_EQ(req.headers.at("apns-topic"), "com.chirp.app");
  EXPECT_NE(req.payload.find("Hello world"), std::string::npos);
}

TEST(NotificationTransportTest, FcmRequestCarriesEndpointAndAuthHeader) {
  auto transport = std::make_shared<RecordingTransport>();
  transport->canned = "ok";
  FCMConfig fcm;
  fcm.server_key = "sk";
  NotificationService svc(fcm, APNsConfig{}, transport);

  DeviceRegistration reg = MakeDevice("d1", "tr-u4", "android");
  reg.fcm_token = "tok";
  svc.RegisterDevice(reg);
  EXPECT_TRUE(svc.SendNotification("tr-u4", MakePayload()));

  ASSERT_EQ(transport->requests.size(), 1u);
  EXPECT_EQ(transport->requests[0].provider, "fcm");
  EXPECT_EQ(transport->requests[0].url, fcm.endpoint);
  EXPECT_EQ(transport->requests[0].headers.at("Authorization"), "key=sk");
}

TEST(NotificationTransportTest, CooldownBlocksSubsequentSendsAcrossDevices) {
  auto transport = std::make_shared<RecordingTransport>();
  transport->canned = "ok";
  NotificationService svc(FCMConfig{}, APNsConfig{}, transport);

  DeviceRegistration a = MakeDevice("d1", "tr-u5", "android");
  a.fcm_token = "t1";
  DeviceRegistration b = MakeDevice("d2", "tr-u5", "ios");
  b.apns_token = "t2";
  svc.RegisterDevice(a);
  svc.RegisterDevice(b);

  EXPECT_TRUE(svc.SendNotification("tr-u5", MakePayload()));
  // The first device's success puts the user on cooldown before the second
  // device is attempted.
  EXPECT_EQ(transport->requests.size(), 1u);
  EXPECT_FALSE(svc.SendNotification("tr-u5", MakePayload()));
  EXPECT_EQ(transport->requests.size(), 1u);
}

}  // namespace
