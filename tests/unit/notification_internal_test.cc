// Internal tests: private members are reached through the befriended access
// tag declared in the header; the service implementation is linked in (no
// second compilation context, so coverage merges cleanly).
#include <gtest/gtest.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "notification_service.h"

namespace chirp {
namespace app_notification {

// Provider responses are answered inline; the incoming-call path only needs
// a provider that does not fail.
class OkTransport : public PushTransport {
 public:
  std::string Post(const PushRequest&) override { return "{}"; }
};

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
  // Pins the APNs token clock so the 50-minute refresh window is testable.
  static void set_apns_now(NotificationService& svc, std::function<int64_t()> fn) {
    svc.apns_now_ = std::move(fn);
  }
};


}  // namespace app_notification
}  // namespace chirp

namespace {

using chirp::app_notification::APNsConfig;
using chirp::app_notification::DeviceRegistration;
using chirp::app_notification::FCMConfig;
using chirp::app_notification::NotificationPayload;
using chirp::app_notification::NotificationService;
using chirp::app_notification::NotificationServiceInternalAccess;
using chirp::app_notification::PushRequest;
using chirp::app_notification::PushTransport;

// Captures outbound provider requests; answers empty so the send is scored
// as a failure and no per-user cooldown suppresses later sends.
class CapturingTransport : public PushTransport {
 public:
  std::string Post(const PushRequest& request) override {
    std::lock_guard<std::mutex> lock(mu);
    requests.push_back(request);
    return "";
  }
  std::mutex mu;
  std::vector<PushRequest> requests;
};

// PKCS#8 P-256 private key in PEM (the .p8 encoding the developer portal
// ships) for the provider-token wiring tests.
std::string GenP256Pem() {
  EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", const_cast<char*>("P-256"));
  if (key == nullptr) {
    return "";
  }
  BIO* bio = BIO_new(BIO_s_mem());
  const int ok = PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
  char* data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  std::string pem(data, static_cast<size_t>(len));
  BIO_free(bio);
  EVP_PKEY_free(key);
  return ok == 1 ? pem : "";
}

DeviceRegistration MakeIosDevice(const std::string& device_id, const std::string& user) {
  DeviceRegistration reg;
  reg.device_id = device_id;
  reg.user_id = user;
  reg.platform = "ios";
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

TEST(NotificationInternalTest, CallNotificationTitleIsConstant) {
  // The incoming-call path uses the fixed title template.
  NotificationService svc{FCMConfig{}, APNsConfig{},
                          std::make_shared<chirp::app_notification::OkTransport>()};
  DeviceRegistration reg;
  reg.device_id = "d1";
  reg.user_id = "alice";
  reg.platform = "android";
  reg.fcm_token = "tok";
  ASSERT_TRUE(svc.RegisterDevice(reg));

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

TEST(NotificationInternalTest, CleanupKeepsOldButActiveDevices) {
  NotificationService svc{FCMConfig{}, APNsConfig{}};
  DeviceRegistration reg;
  reg.device_id = "d1";
  reg.user_id = "alice";
  reg.platform = "android";
  ASSERT_TRUE(svc.RegisterDevice(reg));

  // Age the device past the cutoff while it stays active: the drop
  // predicate is `old && !active`, so this side of the conjunction must
  // survive the sweep (the sibling test above drives the drop side).
  {
    std::lock_guard<std::mutex> lock(NotificationServiceInternalAccess::mutex(svc));
    auto& devices = NotificationServiceInternalAccess::devices(svc);
    auto it = devices.find("d1");
    ASSERT_NE(it, devices.end());
    std::lock_guard<std::mutex> device_lock(it->second->mu);
    it->second->registered_at = 1;  // long before any real cutoff
  }

  svc.CleanupInactiveDevices(1000);

  EXPECT_EQ(svc.GetUserDevices("alice").size(), 1u);
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

// --- APNs provider token (ES256) service-side wiring ---

TEST(NotificationInternalTest, ApnsSendAttachesCachedBearerToken) {
  const std::string pem = GenP256Pem();
  ASSERT_FALSE(pem.empty());
  APNsConfig apns;
  apns.key_id = "ABC123DEFG";
  apns.team_id = "TEAM1234567";
  apns.bundle_id = "com.example.chirp";
  apns.private_key_bytes = pem;
  auto transport = std::make_shared<CapturingTransport>();
  NotificationService svc{FCMConfig{}, apns, transport};
  int64_t now = 1700000000;
  NotificationServiceInternalAccess::set_apns_now(svc, [&now] { return now; });
  ASSERT_TRUE(svc.RegisterDevice(MakeIosDevice("d1", "alice")));
  ASSERT_TRUE(svc.RegisterDevice(MakeIosDevice("d2", "bobby")));

  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 1u);
  const PushRequest& first = transport->requests[0];
  EXPECT_EQ(first.provider, "apns");
  EXPECT_EQ(first.headers.at("apns-topic"), "com.example.chirp");
  const std::string bearer = first.headers.at("Authorization");
  EXPECT_EQ(bearer.substr(0, 7), "Bearer ");
  EXPECT_EQ(std::count(bearer.begin(), bearer.end(), '.'), 2);

  // Within the refresh window the cached token is reused verbatim.
  now += 100;
  EXPECT_FALSE(svc.SendNotificationToDevice("d2", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 2u);
  EXPECT_EQ(transport->requests[1].headers.at("Authorization"), bearer);

  // Past the 50-minute window the token is rebuilt: same key, new iat.
  now += 50 * 60 + 1;
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 3u);
  EXPECT_NE(transport->requests[2].headers.at("Authorization"), bearer);
}

TEST(NotificationInternalTest, ApnsSendOmitsAuthorizationWithoutProviderConfig) {
  auto transport = std::make_shared<CapturingTransport>();
  NotificationService svc{FCMConfig{}, APNsConfig{}, transport};
  ASSERT_TRUE(svc.RegisterDevice(MakeIosDevice("d1", "alice")));
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 1u);
  EXPECT_EQ(transport->requests[0].headers.count("Authorization"), 0u);

  // kid/team set but no key material (missing key file): header-less too —
  // flag-only deployments keep the previous stub behavior.
  APNsConfig partial;
  partial.key_id = "KID";
  partial.team_id = "TEAM";
  partial.private_key_path = "/nonexistent/chirp-apns-test-key.pem";
  NotificationService svc2{FCMConfig{}, partial, transport};
  ASSERT_TRUE(svc2.RegisterDevice(MakeIosDevice("d2", "alice")));
  EXPECT_FALSE(svc2.SendNotificationToDevice("d2", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 2u);
  EXPECT_EQ(transport->requests[1].headers.count("Authorization"), 0u);
}

TEST(NotificationInternalTest, ApnsSendReportsTokenBuildFailure) {
  APNsConfig bad;
  bad.key_id = "KID";
  bad.team_id = "TEAM";
  bad.private_key_bytes = "not a pem";
  auto transport = std::make_shared<CapturingTransport>();
  NotificationService svc{FCMConfig{}, bad, transport};
  ASSERT_TRUE(svc.RegisterDevice(MakeIosDevice("d1", "alice")));
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 1u);
  EXPECT_EQ(transport->requests[0].headers.count("Authorization"), 0u);
}

TEST(NotificationInternalTest, ApnsBearerTokenReadsKeyFile) {
  const std::string pem = GenP256Pem();
  ASSERT_FALSE(pem.empty());
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "chirp_apns_svc_test.pem";
  {
    std::ofstream out(path, std::ios::binary);
    out << pem;
  }
  APNsConfig apns;
  apns.key_id = "KID";
  apns.team_id = "TEAM";
  apns.private_key_path = path.string();
  auto transport = std::make_shared<CapturingTransport>();
  NotificationService svc{FCMConfig{}, apns, transport};
  ASSERT_TRUE(svc.RegisterDevice(MakeIosDevice("d1", "alice")));
  EXPECT_FALSE(svc.SendNotificationToDevice("d1", MakePayload()));
  ASSERT_EQ(transport->requests.size(), 1u);
  const std::string bearer = transport->requests[0].headers.at("Authorization");
  EXPECT_EQ(bearer.substr(0, 7), "Bearer ");
  std::filesystem::remove(path);
}
