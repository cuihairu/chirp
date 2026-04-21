#include <gtest/gtest.h>

#include <memory>

#include "gateway_session_registry.h"
#include "network/session.h"

namespace chirp::gateway {
namespace {

class FakeSession : public chirp::network::Session {
public:
  void Send(std::string) override {}
  void SendAndClose(std::string) override {}
  void Close() override {}
  bool IsClosed() const override { return false; }
};

TEST(GatewaySessionRegistryTest, RebindingSameConnectionRemovesPreviousUserMapping) {
  auto state = std::make_shared<GatewayState>();
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

TEST(GatewaySessionRegistryTest, RebindingUserReturnsOldSessionForKick) {
  auto state = std::make_shared<GatewayState>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", old_session));
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", new_session);

  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), old_session.get());
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).session_id, "s2");
}

TEST(GatewaySessionRegistryTest, RemoveAuthenticatedSessionClearsAllMappings) {
  auto state = std::make_shared<GatewayState>();
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

} // namespace
} // namespace chirp::gateway
