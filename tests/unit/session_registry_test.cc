// Unit tests for the shared authenticated-session registry
// (libs/network/session_registry.{h,cc}), used by gateway, app_gateway and
// chat for their login/kick/logout lifecycle. Sessions are keyed by the
// (user, platform) pair (多端在线：同 platform 顶号、跨 platform 共存，同
// device_id 重连幂等重绑落在同 platform 槽位上)；device_id 只作元数据记录。
// The device-presence helpers (libs/network/device_presence.{h,cc}) are
// exercised here too - they read the same registry.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "network/device_presence.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "proto/auth.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::network {
namespace {

class FakeSession : public Session {
public:
  void Send(std::string) override {}
  void SendAndClose(std::string) override {}
  void Close() override {}
  bool IsClosed() const override { return false; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }
};

// Same shape, but remembers what the presence broadcast wrote to it.
class RecordingSession : public Session {
public:
  std::vector<std::string> sent;

  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string) override {}
  void Close() override {}
  bool IsClosed() const override { return false; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }
};

TEST(SessionRegistryTest, NormalizeDeviceAndPlatformDefaults) {
  EXPECT_EQ(NormalizeDeviceId(""), "default");
  EXPECT_EQ(NormalizeDeviceId("phone-a"), "phone-a");
  EXPECT_EQ(NormalizePlatformId(""), "default");
  EXPECT_EQ(NormalizePlatformId("web"), "web");
}

TEST(SessionRegistryTest, SessionToUserWithoutDeviceMapRebindsCleanly) {
  // A session_to_user entry with no session_to_platform twin: the previous
  // platform defaults to "" and the identity-change arm still runs.
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session, "web"));
  state->session_to_platform.erase(session.get());
  // Rebind as a different user: previous_platform is the "" default.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", "phone-a", session, "web"));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");
}

TEST(SessionRegistryTest, RebindingSameConnectionRemovesPreviousUserMapping) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session, "web"));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "alice");
  EXPECT_EQ(GetAuthenticatedSession(state, session).device_id, "phone-a");
  EXPECT_EQ(GetAuthenticatedSession(state, session).platform, "web");

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", "phone-a", session, "web"));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");
  EXPECT_EQ(GetAuthenticatedSession(state, session).session_id, "s2");

  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  ASSERT_EQ(state->user_to_sessions.count("bob"), 1u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("web").lock().get(), session.get());
}

TEST(SessionRegistryTest, RebindSameIdentityKeepsSlotWithoutErase) {
  // Same (user, platform) on the same connection: the identity-change guard
  // at Bind is false for both operands, so ErasePlatformSlot must not run
  // and the slot stays owned by this session.
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session, "web"));
  // Same identity rebind still returns the slot's current owner (this session)
  // without running ErasePlatformSlot.
  EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", session, "web").get(),
            session.get());
  EXPECT_EQ(GetSession(state, "alice", "web").get(), session.get());
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
  EXPECT_EQ(GetAuthenticatedSession(state, session).session_id, "s2");
}

TEST(SessionRegistryTest, RebindingSameUserAndPlatformReturnsOldSessionForKick) {
  auto state = std::make_shared<SessionRegistry>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", old_session, "web"));
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", "tablet-b", new_session, "web");

  // 多端在线顶号：同 platform（哪怕换了 device_id）也顶掉旧会话。
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), old_session.get());
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).session_id, "s2");
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).device_id, "tablet-b");
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
}

TEST(SessionRegistryTest, DifferentPlatformsOfSameUserCoexist) {
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();
  auto phone = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web"));
  // A second platform of the same user must not kick the first one.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios"));

  EXPECT_EQ(GetSession(state, "alice", "web").get(), web.get());
  EXPECT_EQ(GetSession(state, "alice", "ios").get(), phone.get());
  EXPECT_EQ(GetUserSessions(state, "alice").size(), 2u);
}

TEST(SessionRegistryTest, EmptyPlatformNormalizesToDefaultSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto legacy_a = std::make_shared<FakeSession>();
  auto legacy_b = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "", legacy_a));
  EXPECT_EQ(GetAuthenticatedSession(state, legacy_a).platform, "default");
  // Legacy clients without a platform still kick each other: they all map
  // onto the "default" slot (historical one-session-per-user behavior).
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", "", legacy_b);
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), legacy_a.get());
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
}

TEST(SessionRegistryTest, RebindingSameConnectionAcrossPlatformsMovesSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session, "ios"));
  // The same connection re-login as a different platform: the old slot must
  // not linger as a zombie binding of this connection.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "phone-a", session, "web"));

  EXPECT_EQ(GetSession(state, "alice", "ios"), nullptr);
  EXPECT_EQ(GetSession(state, "alice", "web").get(), session.get());
  EXPECT_EQ(GetUserSessions(state, "alice").size(), 1u);
}

TEST(SessionRegistryTest, RemoveWithExpiredSlotOwnerReportsRelease) {
  // The platform slot's weak_ptr is expired (owner died without Remove) while
  // this session's session_to_* maps still point at the pair: lock() is null
  // and Remove must take the !bound arm and report a release.
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone, "ios"));
  {
    auto transient = std::make_shared<FakeSession>();
    EXPECT_EQ(
        BindAuthenticatedSession(state, "alice", "s2", "phone-a", transient, "ios").get(),
        phone.get());
  }  // transient destroyed: user_to_sessions weak expired, phone's maps remain
  // phone still has session_to_user/platform from its first bind; the slot now
  // has an expired weak. Re-bind phone as the owner of a fresh identity so
  // session_to_* and the expired slot coexist, then Remove.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s3", "phone-a", phone, "ios"));
  // Drop the live owner again without Remove so lock() is null at Remove time.
  {
    auto again = std::make_shared<FakeSession>();
    EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s4", "phone-a", again, "ios").get(),
              phone.get());
  }
  std::string removed_user;
  std::string removed_device;
  std::string removed_platform;
  EXPECT_TRUE(RemoveAuthenticatedSession(state, phone, &removed_user, &removed_device,
                                         &removed_platform));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_device, "phone-a");
  EXPECT_EQ(removed_platform, "ios");
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
}

TEST(SessionRegistryTest, RemoveClearsOnlyOwnPlatformSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios");

  std::string removed_user;
  std::string removed_device;
  std::string removed_platform;
  EXPECT_TRUE(RemoveAuthenticatedSession(state, web, &removed_user, &removed_device,
                                         &removed_platform));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_platform, "web");
  EXPECT_TRUE(GetAuthenticatedSession(state, web).user_id.empty());
  EXPECT_EQ(state->user_to_sessions.count("alice"), 1u);
  EXPECT_EQ(GetSession(state, "alice", "ios").get(), phone.get());

  EXPECT_TRUE(
      RemoveAuthenticatedSession(state, phone, &removed_user, nullptr, &removed_platform));
  EXPECT_EQ(removed_platform, "ios");
  // The user entry disappears together with its last platform slot.
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
}

TEST(SessionRegistryTest, RemoveReportsNoReleaseWhenSlotWasTakenOver) {
  // A kicked session disconnecting late must not report a release: the slot
  // now belongs to the newer session of the same (user, platform).
  auto state = std::make_shared<SessionRegistry>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", old_session, "web");
  BindAuthenticatedSession(state, "alice", "s2", "tab-2", new_session, "web");

  std::string removed_user;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, old_session, &removed_user));
  // The identity is still reported even though nothing was released.
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(GetSession(state, "alice", "web").get(), new_session.get());
}

TEST(SessionRegistryTest, ZombieSessionRebindAfterUserEntryVanished) {
  // A kicked session can reconnect late: its remembered (user, platform)
  // binding points at a user entry that was fully released in between. The
  // stale-binding erase must degrade to a no-op.
  auto state = std::make_shared<SessionRegistry>();
  auto zombie = std::make_shared<FakeSession>();
  auto replacement = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", zombie, "web"));
  ASSERT_EQ(
      BindAuthenticatedSession(state, "alice", "s2", "tab-2", replacement, "web").get(),
      zombie.get());
  // The replacement logs out: the whole user entry goes away.
  EXPECT_TRUE(RemoveAuthenticatedSession(state, replacement));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);

  // The zombie re-logins as another user; nothing of alice may resurface.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "tab-1", zombie, "web"));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("web").lock().get(), zombie.get());
}

TEST(SessionRegistryTest, ZombieSessionRebindAfterPlatformSlotVanished) {
  // Same shape, but only the remembered platform slot is gone while another
  // platform of the user survives: the user entry must be left intact.
  auto state = std::make_shared<SessionRegistry>();
  auto zombie = std::make_shared<FakeSession>();
  auto phone = std::make_shared<FakeSession>();
  auto replacement = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", zombie, "web"));
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios"));
  ASSERT_EQ(
      BindAuthenticatedSession(state, "alice", "s3", "tab-2", replacement, "web").get(),
      zombie.get());
  // The replacement releases the web slot; the ios platform survives.
  EXPECT_TRUE(RemoveAuthenticatedSession(state, replacement));
  EXPECT_EQ(state->user_to_sessions["alice"].count("web"), 0u);

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s4", "tab-1", zombie, "web"));
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
  EXPECT_EQ(GetSession(state, "alice", "ios").get(), phone.get());
  EXPECT_EQ(state->user_to_sessions.count("bob"), 1u);
}

TEST(SessionRegistryTest, LookupsHandleUnknownUsersAndExpiredSlots) {
  auto state = std::make_shared<SessionRegistry>();

  // Unknown users read as empty across every lookup flavor.
  EXPECT_TRUE(GetUserSessions(state, "nobody").empty());
  EXPECT_EQ(GetSession(state, "nobody", "web"), nullptr);
  EXPECT_TRUE(ListOnlineDevices(state, "nobody").empty());

  // A slot left behind by a session that died without disconnect cleanup
  // reads as absent: expired entries are skipped, never surfaced.
  {
    auto ephemeral = std::make_shared<FakeSession>();
    BindAuthenticatedSession(state, "alice", "s1", "tab-1", ephemeral, "web");
  }
  EXPECT_TRUE(GetUserSessions(state, "alice").empty());
  EXPECT_EQ(GetSession(state, "alice", "web"), nullptr);
  EXPECT_TRUE(ListOnlineDevices(state, "alice").empty());
}

TEST(SessionRegistryTest, RemoveUnknownSessionReturnsFalse) {
  auto state = std::make_shared<SessionRegistry>();
  auto bound = std::make_shared<FakeSession>();
  auto stranger = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", bound, "web"));
  // Removing a session that was never bound must not disturb the mapping.
  std::string removed;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, stranger, &removed));
  EXPECT_TRUE(removed.empty());
  EXPECT_EQ(state->session_to_user.count(bound.get()), 1u);
  EXPECT_EQ(state->session_to_user.count(stranger.get()), 0u);
}

TEST(SessionRegistryTest, ErasePlatformSlotSkipsWhenSlotTakenOver) {
  // Session A is kicked by B (same pair), then A rebinds as another user:
  // ErasePlatformSlot must leave B's slot alone (bound.get() != session).
  auto state = std::make_shared<SessionRegistry>();
  auto a = std::make_shared<FakeSession>();
  auto b = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", a, "web"));
  EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "tab-2", b, "web").get(), a.get());
  // A still maps to alice/web in session_to_*; rebind as bob.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "tab-1", a, "web"));
  EXPECT_EQ(GetSession(state, "alice", "web").get(), b.get());
  EXPECT_EQ(state->user_to_sessions.count("alice"), 1u);
  EXPECT_EQ(GetAuthenticatedSession(state, a).user_id, "bob");
}

TEST(SessionRegistryTest, ErasePlatformSlotClearsExpiredSlot) {
  // Same takeover shape, but the new owner died without Remove: the weak_ptr
  // is expired and ErasePlatformSlot must clear the stale slot (!bound).
  auto state = std::make_shared<SessionRegistry>();
  auto a = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "tab-1", a, "web"));
  {
    auto b = std::make_shared<FakeSession>();
    EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "tab-2", b, "web").get(), a.get());
  }  // b destroyed without Remove: user_to_sessions[alice][web] expires
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "tab-1", a, "web"));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("web").lock().get(), a.get());
}

TEST(SessionRegistryTest, RemoveWhenUserEntryAlreadyGoneStillReportsIdentity) {
  // session_to_user still has the pair after the user entry was wiped out of
  // band: identity is reported but nothing is released (returns false).
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  state->user_to_sessions.erase("alice");

  std::string removed_user;
  std::string removed_device;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, web, &removed_user, &removed_device));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_device, "tab-1");
  EXPECT_EQ(state->session_to_user.count(web.get()), 0u);
}

TEST(SessionRegistryTest, RemoveSkipsEraseWhenPlatformMapLostSlot) {
  // session_to_platform still points at web but user_to_sessions lost that
  // platform map entry: the erase path must not report a release.
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  state->user_to_sessions["alice"].erase("web");

  EXPECT_FALSE(RemoveAuthenticatedSession(state, web));
  EXPECT_EQ(state->session_to_user.count(web.get()), 0u);
}

TEST(SessionRegistryTest, RemoveStalePlatformEntryInSessionToPlatform) {
  // session has a user mapping but no platform mapping: platform defaults to
  // "", no platform slot matches, so no release is reported.
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  state->session_to_platform.erase(web.get());

  std::string removed_platform;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, web, nullptr, nullptr, &removed_platform));
  EXPECT_EQ(removed_platform, "");
  EXPECT_EQ(state->session_to_user.count(web.get()), 0u);
}

TEST(SessionRegistryTest, ListOnlineDevicesReportsPlatformAndDevice) {
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios");

  auto devices = ListOnlineDevices(state, "alice");
  ASSERT_EQ(devices.size(), 2u);
  // unordered_map iteration order is unspecified - compare through an
  // ordered projection keyed by platform.
  std::map<std::string, std::string> by_platform;
  for (const auto& info : devices) {
    by_platform[info.platform] = info.device_id;
  }
  ASSERT_EQ(by_platform.size(), 2u);
  EXPECT_EQ(by_platform.at("ios"), "phone-a");
  EXPECT_EQ(by_platform.at("web"), "tab-1");

  // The just-logged-in session is excluded from its own initial listing.
  auto without_web = ListOnlineDevices(state, "alice", web.get());
  ASSERT_EQ(without_web.size(), 1u);
  EXPECT_EQ(without_web[0].platform, "ios");
  EXPECT_EQ(without_web[0].device_id, "phone-a");
}

TEST(SessionRegistryTest, LoginKickReasonNamesPlatformWithLegacyFallback) {
  EXPECT_EQ(LoginKickReason("web"), "logged in on another web");
  EXPECT_EQ(LoginKickReason("ios"), "logged in on another ios");
  // 归一化的 "default" 不该出现在玩家可见文案里：空 platform 回退旧文案。
  EXPECT_EQ(LoginKickReason(""), "logged in on another device");
}

TEST(SessionRegistryTest, FillOnlineDevicesListsOtherLiveBindingsOnly) {
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<FakeSession>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios");

  chirp::auth::LoginResponse resp;
  FillOnlineDevices(state, "alice", &resp, web.get());
  ASSERT_EQ(resp.online_devices_size(), 1);
  EXPECT_EQ(resp.online_devices(0).platform(), "ios");
  EXPECT_EQ(resp.online_devices(0).device_id(), "phone-a");
  EXPECT_TRUE(resp.online_devices(0).online());
  EXPECT_GT(resp.online_devices(0).ts(), 0);

  // No exclude -> both platforms listed.
  chirp::auth::LoginResponse full;
  FillOnlineDevices(state, "alice", &full);
  EXPECT_EQ(full.online_devices_size(), 2);
}

namespace {

// Undoes ProtobufFraming's u32_be length prefix and parses the payload.
chirp::gateway::Packet DecodeFramed(const std::string& framed) {
  chirp::gateway::Packet pkt;
  const uint32_t len = (static_cast<uint32_t>(static_cast<unsigned char>(framed[0])) << 24) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(framed[1])) << 16) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(framed[2])) << 8) |
                       static_cast<uint32_t>(static_cast<unsigned char>(framed[3]));
  EXPECT_EQ(framed.size(), 4u + len);
  EXPECT_TRUE(pkt.ParseFromString(framed.substr(4)));
  return pkt;
}

} // namespace

TEST(SessionRegistryTest, BroadcastDevicePresenceFanOutSkipsExcludedSession) {
  auto state = std::make_shared<SessionRegistry>();
  auto web = std::make_shared<RecordingSession>();
  auto phone = std::make_shared<RecordingSession>();
  auto stranger = std::make_shared<RecordingSession>();

  BindAuthenticatedSession(state, "alice", "s1", "tab-1", web, "web");
  BindAuthenticatedSession(state, "alice", "s2", "phone-a", phone, "ios");
  BindAuthenticatedSession(state, "bob", "s3", "pc-1", stranger, "pc");

  // alice 的另一个端收到上线事件；bob 的端、以及刚上线的 ios 会话自己收不到
  // （生产路径登录广播总是排除刚登录的 session 本身）。
  BroadcastDevicePresence(state, "alice", "ios", "phone-a", /*online=*/true, phone.get());
  ASSERT_EQ(web->sent.size(), 1u);
  chirp::gateway::Packet pkt = DecodeFramed(web->sent[0]);
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::DEVICES_PRESENCE_NOTIFY);
  EXPECT_EQ(pkt.sequence(), 0);
  chirp::auth::DevicesPresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(pkt.body()));
  ASSERT_EQ(notify.devices_size(), 1);
  EXPECT_EQ(notify.devices(0).platform(), "ios");
  EXPECT_EQ(notify.devices(0).device_id(), "phone-a");
  EXPECT_TRUE(notify.devices(0).online());
  EXPECT_GT(notify.devices(0).ts(), 0);

  EXPECT_EQ(phone->sent.size(), 0u);  // 被排除的会话自己不收
  EXPECT_EQ(stranger->sent.size(), 0u);  // 其他用户不收

  // 下线事件带 online=false。
  BroadcastDevicePresence(state, "alice", "ios", "phone-a", /*online=*/false, phone.get());
  ASSERT_EQ(web->sent.size(), 2u);
  chirp::gateway::Packet pkt2 = DecodeFramed(web->sent[1]);
  chirp::auth::DevicesPresenceNotify notify2;
  ASSERT_TRUE(notify2.ParseFromString(pkt2.body()));
  EXPECT_FALSE(notify2.devices(0).online());
}

} // namespace
} // namespace chirp::network
