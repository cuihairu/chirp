// Enhanced chat build (app_chat) session semantics: DistributedChatState
// must share the basic build's (user, device) mutual-kick kernel from
// libs/network/session_registry (TODO: enhanced 会话语义修复) and fan local
// delivery out to every healthy device instead of one user-dimension slot.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <asio.hpp>

#include "delivery_ack_manager.h"
#include "network/message_router.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

// The enhanced main keeps its session internals (DistributedChatState,
// HandleLogin, HandleSendMessage, HealthyLocalSessions, ...) in an anonymous
// namespace; pull the file in with main() renamed so the tests below can
// drive them directly - the same pattern chat_managers_test.cc uses for
// main_distributed.cc.
#define main chirp_chat_enhanced_main
#include "main_enhanced.cc"
#undef main

namespace {

using chirp::chat::DeliveryAckManager;
using chirp::gateway::Packet;

// Pure in-memory Session mock: records every frame sent through it and can
// fake a half-closed peer (the delivery paths treat that as offline).
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  bool PeerHalfClosed() override { return half_closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
  bool half_closed = false;
};

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
bool DecodeFramed(const std::string& framed, Packet* out) {
  if (framed.size() < 4u) {
    return false;
  }
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  if (framed.size() != 4u + static_cast<size_t>(len)) {
    return false;
  }
  return out->ParseFromString(framed.substr(4, len));
}

// Every frame the session received, decoded as a gateway Packet.
std::vector<Packet> FramesOf(const MockSession& session) {
  std::vector<Packet> frames;
  for (const auto& framed : session.sent) {
    Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      frames.push_back(std::move(pkt));
    }
  }
  return frames;
}

std::vector<Packet> FramesOf(const MockSession& session, chirp::gateway::MsgID msg_id) {
  std::vector<Packet> matches;
  for (const auto& pkt : FramesOf(session)) {
    if (pkt.msg_id() == msg_id) {
      matches.push_back(pkt);
    }
  }
  return matches;
}

class EnhancedSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_shared<DistributedChatState>();
    state_->instance_id = "enh-test";

    // A dead Redis port keeps the store deterministic: history/offline
    // writes fail over to the in-memory fallback instead of a shared
    // server, and the io_context is never run so the posted MySQL writes
    // never reach the fake driver either.
    store_config_.redis_port = 1;
    store_config_.mysql_pool_size = 1;
    store_ = std::make_shared<HybridMessageStore>(io_, store_config_);
    tracker_ = std::make_shared<MessageDeliveryTracker>(io_, store_);
    router_ = std::make_shared<chirp::network::MessageRouter>(io_, "127.0.0.1", 1);
  }

  // Scaffold LOGIN_REQ: token doubles as the user id (the verifier stays
  // disabled - nullptr - exactly like a build without --token_secret).
  void Login(const std::shared_ptr<MockSession>& session, const std::string& token,
             const std::string& device_id, int64_t seq) {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id(device_id);
    HandleLogin(req, session, state_, store_, router_, nullptr, nullptr, seq);
  }

  asio::io_context io_;
  MessageStoreConfig store_config_;
  std::shared_ptr<DistributedChatState> state_;
  std::shared_ptr<HybridMessageStore> store_;
  std::shared_ptr<MessageDeliveryTracker> tracker_;
  std::shared_ptr<chirp::network::MessageRouter> router_;
};

// --- DistributedChatState over the shared SessionRegistry -----------------

TEST_F(EnhancedSessionTest, AddSessionSameDeviceReturnsPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "phone", "s1", first), nullptr);
  auto kicked = state_->AddSession("alice", "phone", "s2", second);

  // The same (user, device) pair displaces the previous session so the
  // caller can kick it - never a silent overwrite that orphans the old
  // connection.
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked, first);
  EXPECT_EQ(state_->GetUserId(second), "alice");
  // The displaced session keeps its identity until its (late) disconnect
  // arrives; the registry only moves the device slot.
  EXPECT_EQ(state_->GetUserId(first), "alice");
}

TEST_F(EnhancedSessionTest, AddSessionOtherDeviceCoexists) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "phone", "s1", phone), nullptr);
  EXPECT_EQ(state_->AddSession("alice", "tablet", "s2", tablet), nullptr);

  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);
}

TEST_F(EnhancedSessionTest, AddSessionEmptyDeviceSharesDefaultSlot) {
  auto legacy_a = std::make_shared<MockSession>();
  auto legacy_b = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "", "s1", legacy_a), nullptr);
  // Legacy clients that never send a device id all map onto the "default"
  // slot and keep the historical one-session-per-user kick behavior.
  auto kicked = state_->AddSession("alice", "", "s2", legacy_b);
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked, legacy_a);
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 1u);
}

TEST_F(EnhancedSessionTest, StaleDisconnectKeepsNewerSession) {
  auto old_session = std::make_shared<MockSession>();
  auto new_session = std::make_shared<MockSession>();

  // Re-login takes over the (user, device) slot first; the old connection's
  // disconnect only arrives afterwards (e.g. its late FIN). The stale
  // disconnect must not unregister the session that now owns the slot.
  EXPECT_EQ(state_->AddSession("carol", "phone", "s1", old_session), nullptr);
  ASSERT_TRUE(state_->AddSession("carol", "phone", "s2", new_session));
  state_->RemoveSession(old_session);

  EXPECT_EQ(state_->GetUserId(old_session), "");
  EXPECT_EQ(state_->GetUserId(new_session), "carol");
  auto healthy = HealthyLocalSessions(state_, "carol");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], new_session);
}

TEST_F(EnhancedSessionTest, RemoveSessionOnlyClearsOwnDeviceSlot) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();
  state_->AddSession("alice", "phone", "s1", phone);
  state_->AddSession("alice", "tablet", "s2", tablet);

  state_->RemoveSession(phone);

  EXPECT_EQ(state_->GetUserId(phone), "");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], tablet);

  // Removing an unknown session is a no-op.
  state_->RemoveSession(nullptr);
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
}

TEST_F(EnhancedSessionTest, HealthyLocalSessionsSkipsHalfClosed) {
  auto live = std::make_shared<MockSession>();
  auto half = std::make_shared<MockSession>();
  half->half_closed = true;
  state_->AddSession("alice", "phone", "s1", live);
  state_->AddSession("alice", "tablet", "s2", half);

  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], live);

  EXPECT_TRUE(HealthyLocalSessions(state_, "nobody").empty());
}

// --- HandleLogin: kick contract on top of the registry --------------------

TEST_F(EnhancedSessionTest, LoginSameDeviceKicksPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  Login(first, "alice", "phone", 1);
  auto first_logins = FramesOf(*first, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(first_logins.size(), 1u);
  {
    chirp::auth::LoginResponse resp;
    ASSERT_TRUE(resp.ParseFromString(first_logins[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.user_id(), "alice");
  }

  Login(second, "alice", "phone", 2);

  // The new session logs in with the kick flags set...
  auto second_logins = FramesOf(*second, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(second_logins.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(second_logins[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.kick_previous());
  EXPECT_EQ(resp.kick().reason(), "login from another device");

  // ...and the displaced connection is told instead of silently orphaned.
  auto kicks = FramesOf(*first, chirp::gateway::KICK_NOTIFY);
  ASSERT_EQ(kicks.size(), 1u);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(kicks[0].body()));
  EXPECT_EQ(kick.reason(), "login from another device");
  EXPECT_TRUE(first->close_after_send);

  // The device slot now belongs to the new session only.
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], second);
}

TEST_F(EnhancedSessionTest, LoginOtherDeviceKeepsBothSessions) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();

  Login(phone, "alice", "phone", 1);
  Login(tablet, "alice", "tablet", 2);

  // A second device never kicks the first one.
  EXPECT_EQ(phone->sent.size(), 1u);  // only its LOGIN_RESP
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::KICK_NOTIFY).size(), 0u);
  EXPECT_EQ(FramesOf(*tablet, chirp::gateway::KICK_NOTIFY).size(), 0u);

  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);
}

// --- Local delivery fans out to every healthy device ----------------------

TEST_F(EnhancedSessionTest, PrivateSendFansOutToEveryDevice) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();
  auto sender = std::make_shared<MockSession>();
  state_->AddSession("bob", "phone", "s1", phone);
  state_->AddSession("bob", "tablet", "s2", tablet);

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  HandleSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, router_,
                    /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", /*seq=*/3);

  auto resps = FramesOf(*sender, chirp::gateway::SEND_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.message_id().empty());

  // One copy per device - not one arbitrary slot.
  for (const auto& device : {phone, tablet}) {
    auto notifies = FramesOf(*device, chirp::gateway::CHAT_MESSAGE_NOTIFY);
    ASSERT_EQ(notifies.size(), 1u);
    chirp::chat::ChatMessage msg;
    ASSERT_TRUE(msg.ParseFromString(notifies[0].body()));
    EXPECT_EQ(msg.content(), "hi bob");
    EXPECT_EQ(msg.sender_id(), "alice");
    EXPECT_EQ(msg.receiver_id(), "bob");
    EXPECT_EQ(msg.channel_id(), "alice|bob");
  }
}

TEST_F(EnhancedSessionTest, PrivateSendQueuesOfflineWhenEveryDeviceHalfClosed) {
  auto bob = std::make_shared<MockSession>();
  auto sender = std::make_shared<MockSession>();
  bob->half_closed = true;
  state_->AddSession("bob", "phone", "s1", bob);

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  HandleSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, router_,
                    /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", /*seq=*/4);

  // A half-closed connection counts as offline: nothing is written to it.
  EXPECT_TRUE(bob->sent.empty());

  auto offline = store_->PopOfflineMessages("bob");
  ASSERT_EQ(offline.size(), 1u);
  EXPECT_EQ(offline[0].content, "hi bob");
}

// --- Ack hold: any ack-capable device is enough ---------------------------

TEST_F(EnhancedSessionTest, TrackAckIfCapableHoldsWhenAnyDeviceIsCapable) {
  DeliveryAckManager::Config cfg;
  cfg.timeout_ms = 10000;
  DeliveryAckManager acks(io_, cfg, nullptr, nullptr);

  auto legacy = std::make_shared<MockSession>();
  auto modern = std::make_shared<MockSession>();

  EXPECT_FALSE(TrackAckIfCapable(&acks, {legacy}, "m1", "bob", "payload-1"));
  EXPECT_EQ(acks.pending_count(), 0u);

  acks.MarkCapable(modern);
  EXPECT_TRUE(TrackAckIfCapable(&acks, {legacy, modern}, "m2", "bob", "payload-2"));
  EXPECT_EQ(acks.pending_count(), 1u);
}

}  // namespace
