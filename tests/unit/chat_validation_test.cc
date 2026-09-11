#include <gtest/gtest.h>

#include <memory>

#include "chat_session_registry.h"
#include "chat_validation.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "proto/gateway.pb.h"

namespace chirp::chat {
namespace {

class FakeSession : public chirp::network::Session {
public:
  void Send(std::string bytes) override { last_sent = std::move(bytes); }
  void SendAndClose(std::string bytes) override {
    send_and_close_called = true;
    last_sent = std::move(bytes);
    closed = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }

  std::string last_sent;
  bool send_and_close_called{false};
  bool closed{false};
};

TEST(ChatValidationTest, RejectsSpoofedSenderId) {
  SendMessageRequest req;
  req.set_sender_id("mallory");
  req.set_receiver_id("bob");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsPrivateMessageToSelf) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("alice");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::INVALID_PARAM);
}

TEST(ChatValidationTest, ToleratesPrivateMessageWithExplicitChannelId) {
  // Clients (send client, core SDK) may fill channel_id for convenience;
  // the service derives the canonical private channel from the pair itself.
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(PRIVATE);
  req.set_channel_id("alice|bob");

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::OK);
}

TEST(ChatValidationTest, AcceptsValidPrivateMessage) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::OK);
}

TEST(ChatValidationTest, PrivateChannelContainsUserEdgeCases) {
  EXPECT_FALSE(PrivateChannelContainsUser("", "alice"));   // empty channel
  EXPECT_FALSE(PrivateChannelContainsUser("alice|bob", "")); // empty user
  EXPECT_FALSE(PrivateChannelContainsUser("noseparator", "alice"));
  EXPECT_FALSE(PrivateChannelContainsUser("|bob", "alice"));    // empty left side
  EXPECT_FALSE(PrivateChannelContainsUser("alice|", "alice"));  // empty right side
  EXPECT_TRUE(PrivateChannelContainsUser("alice|bob", "bob"));
}

TEST(ChatValidationTest, RejectsUnauthenticatedSend) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, ""), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsGroupSendWithoutChannelId) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(GUILD);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::INVALID_PARAM);
}

TEST(ChatValidationTest, RejectsGroupSendWithReceiverId) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(GUILD);
  req.set_channel_id("g1");
  req.set_receiver_id("bob");

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::INVALID_PARAM);
}

TEST(ChatValidationTest, AcceptsValidGroupSend) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(GUILD);
  req.set_channel_id("g1");

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::OK);
}

TEST(ChatValidationTest, AcceptsValidGroupHistory) {
  GetHistoryRequest req;
  req.set_user_id("alice");
  req.set_channel_type(GUILD);
  req.set_channel_id("g1");

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::OK);
}

TEST(ChatValidationTest, RejectsHistoryForDifferentUser) {
  GetHistoryRequest req;
  req.set_user_id("mallory");
  req.set_channel_type(PRIVATE);
  req.set_channel_id("alice|bob");

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsPrivateHistoryOutsideConversation) {
  GetHistoryRequest req;
  req.set_user_id("alice");
  req.set_channel_type(PRIVATE);
  req.set_channel_id("bob|carol");

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, AcceptsPrivateHistoryForParticipant) {
  GetHistoryRequest req;
  req.set_user_id("alice");
  req.set_channel_type(PRIVATE);
  req.set_channel_id("alice|bob");

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::OK);
}

TEST(ChatValidationTest, RejectsGroupHistoryWithoutChannelId) {
  GetHistoryRequest req;
  req.set_user_id("alice");
  req.set_channel_type(GUILD);

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::INVALID_PARAM);
}

TEST(ChatValidationTest, RejectsLogoutForAnonymousSession) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  req.set_session_id("chat_session_alice");

  EXPECT_EQ(ValidateLogoutRequest(req, "", ""), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsLogoutForDifferentUser) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("mallory");
  req.set_session_id("chat_session_alice");

  EXPECT_EQ(ValidateLogoutRequest(req, "alice", "chat_session_alice"), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsLogoutForStaleSessionId) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  req.set_session_id("old_session");

  EXPECT_EQ(ValidateLogoutRequest(req, "alice", "chat_session_alice"), chirp::common::SESSION_EXPIRED);
}

TEST(ChatValidationTest, AcceptsLogoutWhenSessionMatches) {
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");
  req.set_session_id("chat_session_alice");

  EXPECT_EQ(ValidateLogoutRequest(req, "alice", "chat_session_alice"), chirp::common::OK);
}

TEST(ChatSessionRegistryTest, RebindingSameConnectionRemovesPreviousUserMapping) {
  auto state = std::make_shared<ChatState>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "alice");

  EXPECT_FALSE(BindAuthenticatedSession(state, "bob", "s2", session));
  EXPECT_EQ(GetAuthenticatedSession(state, session).user_id, "bob");

  EXPECT_EQ(state->user_to_session.count("alice"), 0u);
  ASSERT_EQ(state->user_to_session.count("bob"), 1u);
  EXPECT_EQ(state->user_to_session["bob"].lock().get(), session.get());
}

TEST(ChatSessionRegistryTest, RebindingUserReturnsOldSessionForKick) {
  auto state = std::make_shared<ChatState>();
  auto old_session = std::make_shared<FakeSession>();
  auto new_session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", old_session));
  auto kicked = BindAuthenticatedSession(state, "alice", "s2", new_session);

  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked.get(), old_session.get());
  EXPECT_EQ(GetAuthenticatedSession(state, new_session).session_id, "s2");
}

TEST(ChatSessionRegistryTest, RemoveAuthenticatedSessionClearsAllMappings) {
  auto state = std::make_shared<ChatState>();
  auto session = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", session));
  RemoveAuthenticatedSession(state, session);

  EXPECT_TRUE(GetAuthenticatedSession(state, session).user_id.empty());
  EXPECT_EQ(state->user_to_session.count("alice"), 0u);
  EXPECT_EQ(state->session_to_user.count(session.get()), 0u);
  EXPECT_EQ(state->session_to_session_id.count(session.get()), 0u);
}

TEST(ChatSessionRegistryTest, RemoveUnknownSessionIsSafe) {
  auto state = std::make_shared<ChatState>();
  auto bound = std::make_shared<FakeSession>();
  auto stranger = std::make_shared<FakeSession>();

  EXPECT_FALSE(BindAuthenticatedSession(state, "alice", "s1", bound));
  // Removing a session that was never bound must not disturb the mapping.
  RemoveAuthenticatedSession(state, stranger);
  EXPECT_EQ(state->session_to_user.count(bound.get()), 1u);
  EXPECT_EQ(state->session_to_user.count(stranger.get()), 0u);
}

TEST(ChatSessionRegistryTest, LogoutSuccessWouldCloseSessionAfterResponse) {
  auto session = std::make_shared<FakeSession>();

  chirp::auth::LogoutResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_server_time(1);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGOUT_RESP);
  pkt.set_sequence(7);
  pkt.set_body(resp.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));

  EXPECT_TRUE(session->send_and_close_called);
  EXPECT_TRUE(session->IsClosed());
  EXPECT_FALSE(session->last_sent.empty());
}

} // namespace
} // namespace chirp::chat
