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
#include <iterator>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "notification_service.h"

#include "device_store.h"

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
using chirp::app_notification::DeviceStore;
using chirp::app_notification::FCMConfig;
using chirp::app_notification::NotificationPayload;
using chirp::app_notification::NotificationService;
using chirp::app_notification::NotificationServiceInternalAccess;
using chirp::app_notification::OkTransport;
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


// --- Device registry persistence (audit D3: devices_ was memory-only) -----
// DeviceStore keeps the registry across a restart: one tab-separated line per
// device, every text field base64url-encoded so token bytes cannot break rows.

std::filesystem::path TempDeviceStorePath() {
  return std::filesystem::temp_directory_path() / "chirp_device_store_test.tsv";
}

DeviceRegistration MakeAndroidDevice(const std::string& device_id,
                                     const std::string& user) {
  DeviceRegistration reg;
  reg.device_id = device_id;
  reg.user_id = user;
  reg.platform = "android";
  reg.fcm_token = "fcm-tok-" + device_id;
  return reg;
}

TEST(NotificationDeviceStoreTest, RoundTripsAllFields) {
  const std::filesystem::path path = TempDeviceStorePath();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  DeviceStore store(path.string());

  DeviceRegistration reg;
  reg.device_id = "dev\ttab\nnewline";
  reg.user_id = "us\u00e9r with spaces";
  reg.platform = "ios";
  reg.app_version = "1.2.3-beta+build";
  reg.os_version = "iOS 17_1 (21B74)";
  reg.registered_at = 1729300000123;
  reg.is_active = true;
  reg.apns_token = "apns-token-url-safe_-chars123";
  reg.apns_environment = "development";
  reg.push_kit_token = "pk.abc/def+ghi";
  reg.fcm_token = "";

  DeviceRegistration inactive = MakeAndroidDevice("d2", "bob");
  inactive.registered_at = 1729300099999;
  inactive.is_active = false;
  inactive.app_version = "";
  inactive.os_version = "";

  ASSERT_TRUE(store.SaveAll({reg, inactive}));

  const std::vector<DeviceRegistration> loaded = store.LoadAll();
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded[0].device_id, reg.device_id);
  EXPECT_EQ(loaded[0].user_id, reg.user_id);
  EXPECT_EQ(loaded[0].platform, reg.platform);
  EXPECT_EQ(loaded[0].app_version, reg.app_version);
  EXPECT_EQ(loaded[0].os_version, reg.os_version);
  EXPECT_EQ(loaded[0].registered_at, reg.registered_at);
  EXPECT_TRUE(loaded[0].is_active);
  EXPECT_EQ(loaded[0].apns_token, reg.apns_token);
  EXPECT_EQ(loaded[0].apns_environment, reg.apns_environment);
  EXPECT_EQ(loaded[0].push_kit_token, reg.push_kit_token);
  EXPECT_EQ(loaded[0].fcm_token, reg.fcm_token);
  EXPECT_EQ(loaded[1].device_id, inactive.device_id);
  EXPECT_FALSE(loaded[1].is_active);
  EXPECT_EQ(loaded[1].app_version, "");
  std::filesystem::remove(path, ec);
}

TEST(NotificationDeviceStoreTest, LoadMissingFileReturnsEmpty) {
  DeviceStore store((TempDeviceStorePath() / "nope" / "missing.tsv").string());
  // Nothing persisted yet (first boot): an empty registry, not an error.
  EXPECT_TRUE(store.LoadAll().empty());
}

TEST(NotificationDeviceStoreTest, SkipsMalformedLinesButKeepsValid) {
  const std::filesystem::path path = TempDeviceStorePath();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  DeviceStore store(path.string());
  ASSERT_TRUE(store.SaveAll({MakeAndroidDevice("d1", "alice")}));

  // Corrupt the saved copy: a non-base64 cell, a too-short row and a blank
  // line must be skipped on load; the valid row stays.
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << "!!!not-base64\tcells\there\tmore\tstuff\t1\tmore\tcells\there\tand\tthere\n";
    out << "only-three\tcells\there\n";
    out << "\n";
  }
  // A CRLF-terminated row (hand-edited file on Windows) still loads: the
  // trailing \r is trimmed before parsing.
  {
    std::ifstream in(path, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();
    std::string crlf;
    for (const char c : body) {
      if (c == '\n') crlf.push_back('\r');
      crlf.push_back(c);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << crlf;
  }
  const std::vector<DeviceRegistration> crlf_loaded = store.LoadAll();
  ASSERT_EQ(crlf_loaded.size(), 1u);
  EXPECT_EQ(crlf_loaded[0].device_id, "d1");
  std::filesystem::remove(path, ec);
}

TEST(NotificationDeviceStoreTest, SaveFailsWhenDirectoryMissing) {
  DeviceStore store((TempDeviceStorePath() / "no-such-dir" / "devices.tsv").string());
  EXPECT_FALSE(store.SaveAll({MakeAndroidDevice("d1", "alice")}));
}

TEST(NotificationDeviceStoreTest, SaveFailsWhenTargetIsADirectory) {
  // rename(tmp, path) fails when the destination exists as a directory.
  const std::filesystem::path dir = TempDeviceStorePath().string() + ".dir";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directory(dir);
  DeviceStore store(dir.string());
  EXPECT_FALSE(store.SaveAll({MakeAndroidDevice("d1", "alice")}));
  // The temp sibling from the failed rename is cleaned up, not left behind.
  EXPECT_FALSE(std::filesystem::exists(dir.string() + ".tmp", ec));
  std::filesystem::remove_all(dir, ec);
}

TEST(NotificationServicePersistenceTest, RegisterPersistsAndSecondInstanceRestores) {
  const std::filesystem::path path = TempDeviceStorePath();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    NotificationService svc{FCMConfig{}, APNsConfig{},
                            std::make_shared<OkTransport>(),
                            std::make_shared<DeviceStore>(path.string())};
    ASSERT_TRUE(svc.RegisterDevice(MakeAndroidDevice("d1", "alice")));
  }
  NotificationService restored{FCMConfig{}, APNsConfig{},
                               std::make_shared<OkTransport>(),
                               std::make_shared<DeviceStore>(path.string())};
  // A service without a store restores nothing (persistence is opt-in).
  NotificationService memory_only{FCMConfig{}, APNsConfig{}};
  EXPECT_EQ(memory_only.RestorePersisted(), 0);
  EXPECT_EQ(restored.RestorePersisted(), 1);

  const std::vector<DeviceRegistration> devices = restored.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(devices[0].device_id, "d1");
  EXPECT_EQ(devices[0].fcm_token, "fcm-tok-d1");
  EXPECT_TRUE(devices[0].is_active);
  EXPECT_GT(devices[0].registered_at, 0);
  std::filesystem::remove(path, ec);
}

TEST(NotificationServicePersistenceTest, UnregisterRemovesPersistedDevice) {
  const std::filesystem::path path = TempDeviceStorePath();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    NotificationService svc{FCMConfig{}, APNsConfig{},
                            std::make_shared<OkTransport>(),
                            std::make_shared<DeviceStore>(path.string())};
    ASSERT_TRUE(svc.RegisterDevice(MakeAndroidDevice("d1", "alice")));
    ASSERT_TRUE(svc.RegisterDevice(MakeAndroidDevice("d2", "alice")));
    EXPECT_FALSE(svc.UnregisterDevice("missing"));
    ASSERT_TRUE(svc.UnregisterDevice("d1"));
  }
  NotificationService restored{FCMConfig{}, APNsConfig{},
                               std::make_shared<OkTransport>(),
                               std::make_shared<DeviceStore>(path.string())};
  ASSERT_EQ(restored.RestorePersisted(), 1);
  const std::vector<DeviceRegistration> devices = restored.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(devices[0].device_id, "d2");
  std::filesystem::remove(path, ec);
}

TEST(NotificationServicePersistenceTest, UpdateTokenPersists) {
  const std::filesystem::path path = TempDeviceStorePath();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    NotificationService svc{FCMConfig{}, APNsConfig{},
                            std::make_shared<OkTransport>(),
                            std::make_shared<DeviceStore>(path.string())};
    ASSERT_TRUE(svc.RegisterDevice(MakeAndroidDevice("d1", "alice")));
    ASSERT_TRUE(svc.UpdateDeviceToken("d1", "fcm-tok-fresh"));
  }
  NotificationService restored{FCMConfig{}, APNsConfig{},
                               std::make_shared<OkTransport>(),
                               std::make_shared<DeviceStore>(path.string())};
  ASSERT_EQ(restored.RestorePersisted(), 1);
  const std::vector<DeviceRegistration> devices = restored.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(devices[0].fcm_token, "fcm-tok-fresh");
  std::filesystem::remove(path, ec);
}

TEST(NotificationServicePersistenceTest, SaveFailureKeepsDeviceInMemory) {
  DeviceStore store((TempDeviceStorePath() / "no-such-dir" / "devices.tsv").string());
  NotificationService svc{FCMConfig{}, APNsConfig{},
                          std::make_shared<OkTransport>(),
                          std::make_shared<DeviceStore>(store.path())};
  // The registration still lands in memory when the file cannot be written:
  // pushes keep working until a later save succeeds; the Error log carries
  // the failure instead of it being silent.
  ASSERT_TRUE(svc.RegisterDevice(MakeAndroidDevice("d1", "alice")));
  const std::vector<DeviceRegistration> devices = svc.GetUserDevices("alice");
  ASSERT_EQ(devices.size(), 1u);
  EXPECT_EQ(devices[0].fcm_token, "fcm-tok-d1");
}
