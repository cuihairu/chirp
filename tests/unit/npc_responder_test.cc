// Responder tests with fake inject/ack senders: the poison-pill ack
// branches, the reply injection's contents, and the ack-only-after-OK
// policy.
#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "npc_responder.h"

namespace {

using chirp::common::ErrorCode;
using chirp::common::OK;
using chirp::common::SERVER_UNAVAILABLE;
using chirp::npc::NpcResponder;
using chirp::npc::RuleBasedNpcEngine;
using chirp::server_gateway::EventAckRequest;
using chirp::server_gateway::EventDeliverNotify;
using chirp::server_gateway::MessageInjectRequest;

// Records every inject/ack the responder sends and hands the test the
// pending completion callbacks, so each outcome can be driven explicitly.
class FakeSenders {
 public:
  NpcResponder::InjectSender inject_sender() {
    return [this](const MessageInjectRequest& req,
                  std::function<void(ErrorCode)> cb) {
      injects.push_back(req);
      inject_cbs.push_back(std::move(cb));
    };
  }

  NpcResponder::AckSender ack_sender() {
    return [this](const EventAckRequest& req, std::function<void(ErrorCode)> cb) {
      for (const auto& id : req.event_ids()) {
        acked.push_back(id);
      }
      ack_cbs.push_back(std::move(cb));
    };
  }

  void CompleteInject(size_t i, ErrorCode code) { inject_cbs[i](code); }
  void CompleteAck(size_t i, ErrorCode code) { ack_cbs[i](code); }

  std::vector<MessageInjectRequest> injects;
  std::vector<std::string> acked;
  std::vector<std::function<void(ErrorCode)>> inject_cbs;
  std::vector<std::function<void(ErrorCode)>> ack_cbs;
};

EventDeliverNotify MakeEvent(const std::string& payload,
                             const std::string& type = "npc.player_message",
                             const std::string& id = "evt-1") {
  EventDeliverNotify event;
  event.set_event_id(id);
  event.set_event_type(type);
  event.set_payload(payload);
  return event;
}

std::string UtterancePayload(const std::string& message_id = "m-1",
                             const std::string& sender = "user_2",
                             const std::string& npc = "blacksmith_01",
                             const std::string& content = "hello there") {
  chirp::chat::NpcPlayerUtterance u;
  u.set_message_id(message_id);
  u.set_sender_id(sender);
  u.set_npc_id(npc);
  u.set_content(content);
  u.set_timestamp(42);
  return u.SerializeAsString();
}

class NpcResponderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    engine_ = std::make_unique<RuleBasedNpcEngine>(
        chirp::npc::DemoRules());
    responder_ = std::make_unique<NpcResponder>(
        *engine_, senders_.inject_sender(), senders_.ack_sender());
  }

  FakeSenders senders_;
  std::unique_ptr<RuleBasedNpcEngine> engine_;
  std::unique_ptr<NpcResponder> responder_;
};

TEST_F(NpcResponderTest, ForeignEventTypeIsAckedImmediately) {
  responder_->OnEvent(MakeEvent(UtterancePayload(), "trade.state"));
  EXPECT_TRUE(senders_.injects.empty());
  ASSERT_EQ(senders_.acked.size(), 1u);
  EXPECT_EQ(senders_.acked[0], "evt-1");
}

TEST_F(NpcResponderTest, GarbagePayloadIsAckedImmediately) {
  responder_->OnEvent(MakeEvent("not-a-proto"));
  EXPECT_TRUE(senders_.injects.empty());
  ASSERT_EQ(senders_.acked.size(), 1u);
  EXPECT_EQ(senders_.acked[0], "evt-1");
}

TEST_F(NpcResponderTest, BlankSenderOrNpcIdIsAckedImmediately) {
  responder_->OnEvent(MakeEvent(UtterancePayload("m-1", "", "blacksmith_01")));
  responder_->OnEvent(MakeEvent(UtterancePayload("m-1", "user_2", "")));
  responder_->OnEvent(MakeEvent(UtterancePayload("", "user_2", "blacksmith_01")));
  EXPECT_TRUE(senders_.injects.empty());
  EXPECT_EQ(senders_.acked.size(), 3u);
}

TEST_F(NpcResponderTest, ValidEventProducesNpcInjectionReply) {
  responder_->OnEvent(MakeEvent(UtterancePayload("m-9", "user_5",
                                                 "blacksmith_01", "any QUESTS?")));
  ASSERT_EQ(senders_.injects.size(), 1u);
  const auto& reply = senders_.injects[0];
  EXPECT_EQ(reply.inject_id(), "evt-1");  // event id as the idempotency key
  EXPECT_EQ(reply.sender_kind(), chirp::server_gateway::SENDER_NPC);
  EXPECT_EQ(reply.sender_id(), "npc:blacksmith_01");
  EXPECT_EQ(reply.channel_type(), chirp::chat::PRIVATE);
  EXPECT_EQ(reply.receiver_id(), "user_5");
  EXPECT_EQ(reply.content(),
            "Rats in my cellar! Drive them out and I will forge you a sword.");
  EXPECT_TRUE(senders_.acked.empty());  // not acked before the inject answers

  senders_.CompleteInject(0, OK);
  ASSERT_EQ(senders_.acked.size(), 1u);
  EXPECT_EQ(senders_.acked[0], "evt-1");
}

TEST_F(NpcResponderTest, InjectFailureLeavesEventUnacked) {
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  ASSERT_EQ(senders_.injects.size(), 1u);
  senders_.CompleteInject(0, SERVER_UNAVAILABLE);
  EXPECT_TRUE(senders_.acked.empty());
}

TEST_F(NpcResponderTest, FailedAckIsOnlyLogged) {
  responder_->OnEvent(MakeEvent("not-a-proto"));  // poison-pill ack
  ASSERT_EQ(senders_.acked.size(), 1u);
  senders_.CompleteAck(0, SERVER_UNAVAILABLE);  // must not crash or retry
  EXPECT_EQ(senders_.acked.size(), 1u);
}

TEST_F(NpcResponderTest, RedeliveredAnsweredEventIsAckedWithoutNewReply) {
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  ASSERT_EQ(senders_.injects.size(), 1u);
  senders_.CompleteInject(0, OK);
  ASSERT_EQ(senders_.acked.size(), 1u);

  // The hub lost the ack and redelivers: the answer must not repeat.
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  EXPECT_EQ(senders_.injects.size(), 1u);
  ASSERT_EQ(senders_.acked.size(), 2u);
  EXPECT_EQ(senders_.acked[1], "evt-1");  // ack again to stop the retry loop
}

TEST_F(NpcResponderTest, FailedInjectIsNotRememberedAndRedeliveryRetries) {
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  ASSERT_EQ(senders_.injects.size(), 1u);
  senders_.CompleteInject(0, SERVER_UNAVAILABLE);
  EXPECT_TRUE(senders_.acked.empty());

  // Not remembered: the redelivery must attempt the reply again.
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  ASSERT_EQ(senders_.injects.size(), 2u);
  senders_.CompleteInject(1, OK);
  ASSERT_EQ(senders_.acked.size(), 1u);
  EXPECT_EQ(senders_.acked[0], "evt-1");

  // From now on it is answered: a further redelivery only acks.
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  EXPECT_EQ(senders_.injects.size(), 2u);
  EXPECT_EQ(senders_.acked.size(), 2u);
}

TEST_F(NpcResponderTest, DedupeWindowEvictsOldestEvent) {
  responder_ = std::make_unique<NpcResponder>(
      *engine_, senders_.inject_sender(), senders_.ack_sender(),
      /*dedupe_capacity=*/1);

  responder_->OnEvent(MakeEvent(UtterancePayload(), "npc.player_message", "evt-a"));
  senders_.CompleteInject(0, OK);
  responder_->OnEvent(MakeEvent(UtterancePayload(), "npc.player_message", "evt-b"));
  senders_.CompleteInject(1, OK);
  EXPECT_EQ(senders_.injects.size(), 2u);

  // evt-a was evicted by evt-b: its redelivery answers again.
  responder_->OnEvent(MakeEvent(UtterancePayload(), "npc.player_message", "evt-a"));
  EXPECT_EQ(senders_.injects.size(), 3u);
}

TEST_F(NpcResponderTest, DuplicateBeforeOutcomeIsNotSuppressed) {
  // Two deliveries of the same event, neither inject answered yet: both
  // attempt a reply (the hub delivers serially; this only pins the
  // semantics of an in-flight duplicate).
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  responder_->OnEvent(MakeEvent(UtterancePayload()));
  EXPECT_EQ(senders_.injects.size(), 2u);

  senders_.CompleteInject(0, OK);
  senders_.CompleteInject(1, OK);
  EXPECT_EQ(senders_.acked.size(), 2u);
}

}  // namespace
