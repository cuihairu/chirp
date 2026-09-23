// Unit tests for the shared authenticated-session registry
// (libs/network/session_registry.{h,cc}), used by gateway, app_gateway and
// chat for their login/kick/logout lifecycle. Sessions are keyed by the
// (user, device) pair: the same pair kicks, different devices coexist.

#include <gtest/gtest.h>

#include <memory>

#include "network/session.h"
#include "network/session_registry.h"

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

TEST(SessionRegistryTest, NormalizeDeviceIdDefaultsEmptyToDevice) {
  EXPECT_EQ(NormalizeDeviceId(""), "default");
  EXPECT_EQ(NormalizeDeviceId("phone-a"), "phone-a");
}

TEST(SessionRegistryTest, SessionToUserWithoutDeviceMapRebindsCleanly) {
  // A session_to_user entry with no session_to_device twin: the previous
  // device id defaults to "" and the identity-change arm still runs.
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session));
  state->session_to_device.erase(session.get());
  // Rebind as a different user: previous_device_id is the "" default.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", "phone-a", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");
}

TEST(SessionRegistryTest, RebindingSameConnectionRemovesPreviousUserMapping) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "alice");
  EXPECT_EQ(GetAuthenticatedSession(state, session).device_id, "phone-a");

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", "phone-a", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");
  EXPECT_EQ(GetAuthenticatedSession(state, session).session_id, "s2");

  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  ASSERT_EQ(state->user_to_sessions.count("bob"), 1u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("phone-a").lock().get(), session.get());
}

TEST(SessionRegistryTest, RebindSameIdentityKeepsSlotWithoutErase) {
  // Same (user, device) on the same connection: the identity-change guard at
  // Bind is false for both operands, so EraseDeviceSlot must not run and the
  // slot stays owned by this session.
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session));
  // Same identity rebind still returns the slot's current owner (this session)
  // without running EraseDeviceSlot.
  EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", session).get(),
            session.get());
  EXPECT_EQ(GetSession(state, "alice", "phone-a").get(), session.get());
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
  EXPECT_EQ(GetAuthenticatedSession(state, session).session_id, "s2");
}

TEST(SessionRegistryTest, RebindingSameUserAndDeviceReturnsOldSessionForKick) {
  auto state = std::make_shared<SessionRegistry>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", old_session));
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", "phone-a", new_session);

  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), old_session.get());
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).session_id, "s2");
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
}

TEST(SessionRegistryTest, DifferentDevicesOfSameUserCoexist) {
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();
  auto tablet = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone));
  // A second device of the same user must not kick the first one.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "tablet-b", tablet));

  EXPECT_EQ(GetSession(state, "alice", "phone-a").get(), phone.get());
  EXPECT_EQ(GetSession(state, "alice", "tablet-b").get(), tablet.get());
  EXPECT_EQ(GetUserSessions(state, "alice").size(), 2u);
}

TEST(SessionRegistryTest, EmptyDeviceNormalizesToDefaultSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto legacy_a = std::make_shared<FakeSession>();
  auto legacy_b = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "", legacy_a));
  EXPECT_EQ(GetAuthenticatedSession(state, legacy_a).device_id, "default");
  // Legacy clients without a device id still kick each other: they all map
  // onto the "default" slot.
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", "", legacy_b);
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), legacy_a.get());
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
}

TEST(SessionRegistryTest, RebindingSameConnectionAcrossDevicesMovesSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", session));
  // The same connection re-login as a different device: the old slot must
  // not linger as a zombie binding of this connection.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "tablet-b", session));

  EXPECT_EQ(GetSession(state, "alice", "phone-a"), nullptr);
  EXPECT_EQ(GetSession(state, "alice", "tablet-b").get(), session.get());
  EXPECT_EQ(GetUserSessions(state, "alice").size(), 1u);
}

TEST(SessionRegistryTest, RemoveWithExpiredSlotOwnerReportsRelease) {
  // The device slot's weak_ptr is expired (owner died without Remove) while
  // this session's session_to_* maps still point at the pair: lock() is null
  // and Remove must take the !bound arm and report a release.
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone));
  {
    auto transient = std::make_shared<FakeSession>();
    EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", transient).get(),
              phone.get());
  }  // transient destroyed: user_to_sessions weak expired, phone's maps remain
  // phone still has session_to_user/device from its first bind; the slot now
  // has an expired weak. Re-bind phone as the owner of a fresh identity so
  // session_to_* and the expired slot coexist, then Remove.
  // Simpler: rebind phone onto the same (user, device) after the owner died.
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s3", "phone-a", phone));
  // Drop the live owner again without Remove so lock() is null at Remove time.
  {
    auto again = std::make_shared<FakeSession>();
    EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s4", "phone-a", again).get(),
              phone.get());
  }
  std::string removed_user;
  std::string removed_device;
  EXPECT_TRUE(RemoveAuthenticatedSession(state, phone, &removed_user, &removed_device));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_device, "phone-a");
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
}

TEST(SessionRegistryTest, RemoveClearsOnlyOwnDeviceSlot) {
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();
  auto tablet = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone);
  BindAuthenticatedSession(state, "alice", "s2", "tablet-b", tablet);

  std::string removed_user;
  std::string removed_device;
  EXPECT_TRUE(RemoveAuthenticatedSession(state, phone, &removed_user, &removed_device));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_device, "phone-a");
  EXPECT_TRUE(GetAuthenticatedSession(state, phone).user_id.empty());
  EXPECT_EQ(state->user_to_sessions.count("alice"), 1u);
  EXPECT_EQ(GetSession(state, "alice", "tablet-b").get(), tablet.get());

  EXPECT_TRUE(RemoveAuthenticatedSession(state, tablet, &removed_user, &removed_device));
  EXPECT_EQ(removed_device, "tablet-b");
  // The user entry disappears together with its last device slot.
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
}

TEST(SessionRegistryTest, RemoveReportsNoReleaseWhenSlotWasTakenOver) {
  // A kicked session disconnecting late must not report a release: the slot
  // now belongs to the newer session of the same (user, device).
  auto state = std::make_shared<SessionRegistry>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "phone-a", old_session);
  BindAuthenticatedSession(state, "alice", "s2", "phone-a", new_session);

  std::string removed_user;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, old_session, &removed_user));
  // The identity is still reported even though nothing was released.
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(GetSession(state, "alice", "phone-a").get(), new_session.get());
}

TEST(SessionRegistryTest, ZombieSessionRebindAfterUserEntryVanished) {
  // A kicked session can reconnect late: its remembered (user, device)
  // binding points at a user entry that was fully released in between. The
  // stale-binding erase must degrade to a no-op.
  auto state = std::make_shared<SessionRegistry>();
  auto zombie = std::make_shared<FakeSession>();
  auto replacement = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", zombie));
  ASSERT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", replacement).get(),
            zombie.get());
  // The replacement logs out: the whole user entry goes away.
  EXPECT_TRUE(RemoveAuthenticatedSession(state, replacement));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);

  // The zombie re-logins as another user; nothing of alice may resurface.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "phone-a", zombie));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("phone-a").lock().get(), zombie.get());
}

TEST(SessionRegistryTest, ZombieSessionRebindAfterDeviceSlotVanished) {
  // Same shape, but only the remembered device slot is gone while another
  // device of the user survives: the user entry must be left intact.
  auto state = std::make_shared<SessionRegistry>();
  auto zombie = std::make_shared<FakeSession>();
  auto tablet = std::make_shared<FakeSession>();
  auto replacement = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", zombie));
  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s2", "tablet-b", tablet));
  ASSERT_EQ(BindAuthenticatedSession(state, "alice", "s3", "phone-a", replacement).get(),
            zombie.get());
  // The replacement releases the phone slot; the tablet device survives.
  EXPECT_TRUE(RemoveAuthenticatedSession(state, replacement));
  EXPECT_EQ(state->user_to_sessions["alice"].count("phone-a"), 0u);

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s4", "phone-a", zombie));
  EXPECT_EQ(state->user_to_sessions["alice"].size(), 1u);
  EXPECT_EQ(GetSession(state, "alice", "tablet-b").get(), tablet.get());
  EXPECT_EQ(state->user_to_sessions.count("bob"), 1u);
}

TEST(SessionRegistryTest, LookupsHandleUnknownUsersAndExpiredSlots) {
  auto state = std::make_shared<SessionRegistry>();

  // Unknown users read as empty across every lookup flavor.
  EXPECT_TRUE(GetUserSessions(state, "nobody").empty());
  EXPECT_EQ(GetSession(state, "nobody", "phone-a"), nullptr);

  // A slot left behind by a session that died without disconnect cleanup
  // reads as absent: expired entries are skipped, never surfaced.
  {
    auto ephemeral = std::make_shared<FakeSession>();
    BindAuthenticatedSession(state, "alice", "s1", "phone-a", ephemeral);
  }
  EXPECT_TRUE(GetUserSessions(state, "alice").empty());
  EXPECT_EQ(GetSession(state, "alice", "phone-a"), nullptr);
}

TEST(SessionRegistryTest, RemoveUnknownSessionReturnsFalse) {
  auto state = std::make_shared<SessionRegistry>();
  auto bound = std::make_shared<FakeSession>();
  auto stranger = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", bound));
  // Removing a session that was never bound must not disturb the mapping.
  std::string removed;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, stranger, &removed));
  EXPECT_TRUE(removed.empty());
  EXPECT_EQ(state->session_to_user.count(bound.get()), 1u);
  EXPECT_EQ(state->session_to_user.count(stranger.get()), 0u);
}

TEST(SessionRegistryTest, EraseDeviceSlotSkipsWhenSlotTakenOver) {
  // Session A is kicked by B (same pair), then A rebinds as another user:
  // EraseDeviceSlot must leave B's slot alone (bound.get() != session).
  auto state = std::make_shared<SessionRegistry>();
  auto a = std::make_shared<FakeSession>();
  auto b = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", a));
  EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", b).get(), a.get());
  // A still maps to alice/phone-a in session_to_*; rebind as bob.
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "phone-a", a));
  EXPECT_EQ(GetSession(state, "alice", "phone-a").get(), b.get());
  EXPECT_EQ(state->user_to_sessions.count("alice"), 1u);
  EXPECT_EQ(GetAuthenticatedSession(state, a).user_id, "bob");
}

TEST(SessionRegistryTest, EraseDeviceSlotClearsExpiredSlot) {
  // Same takeover shape, but the new owner died without Remove: the weak_ptr
  // is expired and EraseDeviceSlot must clear the stale slot (!bound).
  auto state = std::make_shared<SessionRegistry>();
  auto a = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", "phone-a", a));
  {
    auto b = std::make_shared<FakeSession>();
    EXPECT_EQ(BindAuthenticatedSession(state, "alice", "s2", "phone-a", b).get(), a.get());
  }  // b destroyed without Remove: user_to_sessions[alice][phone-a] expires
  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s3", "phone-a", a));
  EXPECT_EQ(state->user_to_sessions.count("alice"), 0u);
  EXPECT_EQ(state->user_to_sessions["bob"].at("phone-a").lock().get(), a.get());
}

TEST(SessionRegistryTest, RemoveWhenUserEntryAlreadyGoneStillReportsIdentity) {
  // session_to_user still has the pair after the user entry was wiped out of
  // band: identity is reported but nothing is released (returns false).
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone);
  state->user_to_sessions.erase("alice");

  std::string removed_user;
  std::string removed_device;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, phone, &removed_user, &removed_device));
  EXPECT_EQ(removed_user, "alice");
  EXPECT_EQ(removed_device, "phone-a");
  EXPECT_EQ(state->session_to_user.count(phone.get()), 0u);
}

TEST(SessionRegistryTest, RemoveSkipsEraseWhenDeviceMapLostSlot) {
  // session_to_device still points at phone-a but user_to_sessions lost that
  // device map entry: the erase path must not report a release.
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone);
  state->user_to_sessions["alice"].erase("phone-a");

  EXPECT_FALSE(RemoveAuthenticatedSession(state, phone));
  EXPECT_EQ(state->session_to_user.count(phone.get()), 0u);
}

TEST(SessionRegistryTest, RemoveStaleDeviceEntryInSessionToDevice) {
  // session has a user mapping but no device mapping: device id defaults to
  // "", no device slot matches, so no release is reported.
  auto state = std::make_shared<SessionRegistry>();
  auto phone = std::make_shared<FakeSession>();

  BindAuthenticatedSession(state, "alice", "s1", "phone-a", phone);
  state->session_to_device.erase(phone.get());

  std::string removed_device;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, phone, nullptr, &removed_device));
  EXPECT_EQ(removed_device, "");
  EXPECT_EQ(state->session_to_user.count(phone.get()), 0u);
}

} // namespace
} // namespace chirp::network
