// Unit tests for the shared authenticated-session registry
// (libs/network/session_registry.{h,cc}), used by gateway, app_gateway and
// chat for their login/kick/logout lifecycle.

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

TEST(SessionRegistryTest, RebindingSameConnectionRemovesPreviousUserMapping) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "alice");

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");
  EXPECT_EQ(GetAuthenticatedSession(state, session).session_id, "s2");

  EXPECT_EQ(state->user_to_session.count("alice"), 0u);
  ASSERT_EQ(state->user_to_session.count("bob"), 1u);
  EXPECT_EQ(state->user_to_session["bob"].lock().get(), session.get());
}

TEST(SessionRegistryTest, RebindingUserReturnsOldSessionForKick) {
  auto state = std::make_shared<SessionRegistry>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", old_session));
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", new_session);

  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), old_session.get());
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).session_id, "s2");
}

TEST(SessionRegistryTest, RemoveAuthenticatedSessionClearsAllMappings) {
  auto state = std::make_shared<SessionRegistry>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", session));

  std::string removed_user_id;
  EXPECT_TRUE(RemoveAuthenticatedSession(state, session, &removed_user_id));
  EXPECT_EQ(removed_user_id, "alice");
  EXPECT_TRUE(GetAuthenticatedSession(state, session).user_id.empty());
  EXPECT_EQ(state->user_to_session.count("alice"), 0u);
  EXPECT_EQ(state->session_to_user.count(session.get()), 0u);
  EXPECT_EQ(state->session_to_session_id.count(session.get()), 0u);
}

TEST(SessionRegistryTest, RemoveUnknownSessionReturnsFalse) {
  auto state = std::make_shared<SessionRegistry>();
  auto bound = std::make_shared<FakeSession>();
  auto stranger = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", bound));
  // Removing a session that was never bound must not disturb the mapping.
  std::string removed;
  EXPECT_FALSE(RemoveAuthenticatedSession(state, stranger, &removed));
  EXPECT_TRUE(removed.empty());
  EXPECT_EQ(state->session_to_user.count(bound.get()), 1u);
  EXPECT_EQ(state->session_to_user.count(stranger.get()), 0u);
}

} // namespace
} // namespace chirp::network
