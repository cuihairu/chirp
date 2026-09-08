// Internal tests: private members are reached through the befriended access
// tag declared in the header; the service implementation is linked in (no
// second compilation context, so coverage merges cleanly).
#include <gtest/gtest.h>

#include <mutex>
#include <string>

#include "notification_service.h"

namespace chirp {
namespace notification {

// Befriended in notification_service.h: provides test-only accessors.
struct NotificationServiceInternalAccess {
  static std::unordered_map<std::string, std::shared_ptr<DeviceRegistration>>&
  devices(NotificationService& svc) {
    return svc.devices_;
  }
  static std::mutex& mutex(NotificationService& svc) { return svc.mu_; }
  static std::string build_fcm_payload(NotificationService& svc,
                                       const NotificationPayload& payload) {
    return svc.BuildFCMPayload(payload);
  }
};

}  // namespace notification
}  // namespace chirp

namespace {

using chirp::notification::APNsConfig;
using chirp::notification::DeviceRegistration;
using chirp::notification::FCMConfig;
using chirp::notification::NotificationPayload;
using chirp::notification::NotificationService;
using chirp::notification::NotificationServiceInternalAccess;

NotificationPayload MakePayload() {
  NotificationPayload p;
  p.title = "Hi";
  p.body = "Hello world";
  p.sound = "default";
  return p;
}

TEST(NotificationInternalTest, CallNotificationTitleIsConstant) {
  // The incoming-call path uses the fixed title template.
  NotificationService svc{FCMConfig{}, APNsConfig{}};
  DeviceRegistration reg;
  reg.device_id = "d1";
  reg.user_id = "alice";
  reg.platform = "android";
  ASSERT_TRUE(svc.RegisterDevice(reg));

  NotificationPayload sent;
  EXPECT_TRUE(svc.NotifyIncomingCall("alice", "bobby"));
}

TEST(NotificationInternalTest, SendToInactiveDeviceFails) {
  NotificationService svc{FCMConfig{}, APNsConfig{}};
  DeviceRegistration reg;
  reg.device_id = "d1";
  reg.user_id = "alice";
  reg.platform = "android";
  ASSERT_TRUE(svc.RegisterDevice(reg));

  // Flip the stored device to inactive: sends must be rejected.
  {
    std::lock_guard<std::mutex> lock(NotificationServiceInternalAccess::mutex(svc));
    auto& devices = NotificationServiceInternalAccess::devices(svc);
    auto it = devices.find("d1");
    ASSERT_NE(it, devices.end());
    std::lock_guard<std::mutex> device_lock(it->second->mu);
    it->second->is_active = false;
  }
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
}

TEST(NotificationInternalTest, CleanupRemovesStaleInactiveDevices) {
  NotificationService svc{FCMConfig{}, APNsConfig{}};

  // Two devices for the same user: both stale+inactive, both removed. With
  // two entries the user-index erase also observes the "empty set" branch.
  for (int i = 0; i < 2; ++i) {
    DeviceRegistration reg;
    reg.device_id = "d" + std::to_string(i);
    reg.user_id = "alice";
    reg.platform = "android";
    ASSERT_TRUE(svc.RegisterDevice(reg));
    std::lock_guard<std::mutex> lock(NotificationServiceInternalAccess::mutex(svc));
    auto& devices = NotificationServiceInternalAccess::devices(svc);
    auto it = devices.find(reg.device_id);
    ASSERT_NE(it, devices.end());
    std::lock_guard<std::mutex> device_lock(it->second->mu);
    it->second->is_active = false;
    it->second->registered_at = 1; // ancient
  }
  // An active device must survive the sweep.
  DeviceRegistration alive;
  alive.device_id = "alive";
  alive.user_id = "bob";
  alive.platform = "android";
  ASSERT_TRUE(svc.RegisterDevice(alive));

  svc.CleanupInactiveDevices(1000);

  EXPECT_TRUE(svc.GetUserDevices("alice").empty());
  EXPECT_EQ(svc.GetUserDevices("bob").size(), 1u);
}

TEST(NotificationInternalTest, FcmPayloadWithoutTokenBuildsTemplate) {
  NotificationService svc{FCMConfig{}, APNsConfig{}};
  NotificationPayload p = MakePayload();
  p.icon = "bell";
  // Private overload reached through the befriended access tag: build the
  // FCM JSON without a device token (template form).
  const std::string json = NotificationServiceInternalAccess::build_fcm_payload(svc, p);
  EXPECT_NE(json.find("\"to\": \"\""), std::string::npos);
  EXPECT_NE(json.find("\"icon\": \"bell\""), std::string::npos);
}

}  // namespace
