#include <gtest/gtest.h>

#include <memory>

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
  std::string RemoteAddress() const override { return "127.0.0.1"; }
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

TEST(ChatValidationTest, RejectsEmptySenderEvenWhenAuthenticated) {
  SendMessageRequest req;
  req.set_receiver_id("bob");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsPrivateMessageWithoutReceiver) {
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(PRIVATE);

  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::INVALID_PARAM);
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

TEST(ChatValidationTest, RejectsHistoryWithoutAuthentication) {
  GetHistoryRequest req;
  req.set_user_id("alice");
  req.set_channel_type(GUILD);
  req.set_channel_id("g1");

  EXPECT_EQ(ValidateGetHistoryRequest(req, ""), chirp::common::AUTH_FAILED);
}

TEST(ChatValidationTest, RejectsHistoryWithEmptyUserId) {
  GetHistoryRequest req;
  req.set_channel_type(GUILD);
  req.set_channel_id("g1");

  EXPECT_EQ(ValidateGetHistoryRequest(req, "alice"), chirp::common::AUTH_FAILED);
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

TEST(ChatValidationTest, AcceptsLogoutWithEmptySessionId) {
  // A request that never carried a session id skips the mismatch check.
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");

  EXPECT_EQ(ValidateLogoutRequest(req, "alice", "chat_session_alice"), chirp::common::OK);
}

TEST(ChatValidationTest, RejectsLogoutForAuthenticatedEmptyUserId) {
  // Authenticated session but the logout body omitted user_id: the empty
  // arm of `user_id.empty() || user_id != authenticated` fires first.
  chirp::auth::LogoutRequest req;
  req.set_session_id("chat_session_alice");

  EXPECT_EQ(ValidateLogoutRequest(req, "alice", "chat_session_alice"), chirp::common::AUTH_FAILED);
}

// --- content length limits (game_chat_features P0 消息长度限制) ---

namespace {

// Repeats a 3-byte CJK character n times.
std::string CjkChars(int n) {
  std::string out;
  out.reserve(static_cast<size_t>(n) * 3);
  for (int i = 0; i < n; ++i) {
    out += "你";
  }
  return out;
}

} // namespace

TEST(ChatValidationTest, MaxContentCharsCoversTheCappedChannels) {
  EXPECT_EQ(MaxContentChars(PRIVATE), 200u);
  EXPECT_EQ(MaxContentChars(WORLD), 100u);
  EXPECT_EQ(MaxContentChars(SYSTEM_CHANNEL), 500u);
  // Coordination surfaces are uncapped.
  EXPECT_EQ(MaxContentChars(TEAM), 0u);
  EXPECT_EQ(MaxContentChars(GUILD), 0u);
  EXPECT_EQ(MaxContentChars(MARQUEE), 0u);
}

TEST(ChatValidationTest, PrivateContentAtTheLimitIsAccepted) {
  SendMessageRequest req;
  req.set_channel_type(PRIVATE);
  req.set_content(std::string(200, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::OK);

  // 200 CJK chars = 600 UTF-8 bytes but exactly 200 code points.
  req.set_content(CjkChars(200));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::OK);
}

TEST(ChatValidationTest, PrivateContentOverTheLimitIsRejected) {
  SendMessageRequest req;
  req.set_channel_type(PRIVATE);
  req.set_content(std::string(201, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::CONTENT_TOO_LONG);

  req.set_content(CjkChars(201));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::CONTENT_TOO_LONG);
}

TEST(ChatValidationTest, MixedScriptIsCountedInCodePoints) {
  SendMessageRequest req;
  req.set_channel_type(PRIVATE);
  // 100 ASCII + 100 CJK = 200 code points in 500 bytes: accepted, which a
  // byte-counting limit would have rejected long before.
  req.set_content(std::string(100, 'a') + CjkChars(100));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::OK);
}

TEST(ChatValidationTest, WorldContentLimitIsEnforced) {
  SendMessageRequest req;
  req.set_channel_type(WORLD);
  req.set_content(std::string(100, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::OK);

  req.set_content(std::string(101, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::CONTENT_TOO_LONG);
}

TEST(ChatValidationTest, SystemChannelContentLimitIsEnforced) {
  SendMessageRequest req;
  req.set_channel_type(SYSTEM_CHANNEL);
  req.set_content(std::string(500, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::OK);

  req.set_content(std::string(501, 'a'));
  EXPECT_EQ(ValidateContentLength(req), chirp::common::CONTENT_TOO_LONG);
}

TEST(ChatValidationTest, ValidateSendMessageRequestPropagatesContentTooLong) {
  // P1 专码批次：合并校验器把长度超限的专码原样上抛（此前折叠为
  // INVALID_PARAM），发送入口直接回给客户端。
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(PRIVATE);
  req.set_receiver_id("bob");
  req.set_content(std::string(201, 'a'));
  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::CONTENT_TOO_LONG);
}

TEST(ChatValidationTest, TeamGuildAndMarqueeAreUnlimited) {
  SendMessageRequest req;
  req.set_content(std::string(10'000, 'a'));
  for (const ChannelType type : {TEAM, GUILD, MARQUEE}) {
    req.set_channel_type(type);
    EXPECT_EQ(ValidateContentLength(req), chirp::common::OK) << "channel " << static_cast<int>(type);
  }
}

TEST(ChatValidationTest, SendValidationAppliesTheWorldLengthLimit) {
  // The cap is wired into ValidateSendMessageRequest, so the basic form gets
  // it from the shared validation path without any extra wiring.
  SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(WORLD);
  req.set_channel_id("world");

  req.set_content(std::string(100, 'a'));
  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::OK);

  // P1 专码批次后超长在合并校验器里就是 CONTENT_TOO_LONG（不再折叠）。
  req.set_content(std::string(101, 'a'));
  EXPECT_EQ(ValidateSendMessageRequest(req, "alice"), chirp::common::CONTENT_TOO_LONG);
}

// ---------------------------------------------------------------------------
// Channel-type allowlist parsing (--recall_channels and friends)
// ---------------------------------------------------------------------------

TEST(ChatValidationTest, ParseChannelTypeListReadsCsvNames) {
  const auto types = ParseChannelTypeList("private,guild");
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], PRIVATE);
  EXPECT_EQ(types[1], GUILD);

  // Trimming, case folding, the system_channel alias and a trailing comma.
  const auto messy = ParseChannelTypeList(" WORLD , system_channel ,marquee,");
  ASSERT_EQ(messy.size(), 3u);
  EXPECT_EQ(messy[0], WORLD);
  EXPECT_EQ(messy[1], SYSTEM_CHANNEL);
  EXPECT_EQ(messy[2], MARQUEE);

  // Unknown tokens are dropped, so a typo narrows the list instead of
  // widening it.
  const auto typo = ParseChannelTypeList("private,guidl,team");
  ASSERT_EQ(typo.size(), 2u);
  EXPECT_EQ(typo[0], PRIVATE);
  EXPECT_EQ(typo[1], TEAM);

  // Empty / whitespace-only lists allow nothing.
  EXPECT_TRUE(ParseChannelTypeList("").empty());
  EXPECT_TRUE(ParseChannelTypeList(" , , ").empty());
}

TEST(ChatValidationTest, ChannelTypeInListMatchesOnlyListedTypes) {
  const auto types = ParseChannelTypeList("private,guild");
  EXPECT_TRUE(ChannelTypeInList(types, PRIVATE));
  EXPECT_TRUE(ChannelTypeInList(types, GUILD));
  EXPECT_FALSE(ChannelTypeInList(types, WORLD));
  EXPECT_FALSE(ChannelTypeInList({}, PRIVATE));
}

TEST(SessionCloseBehaviorTest, LogoutSuccessWouldCloseSessionAfterResponse) {
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
