// Pure-logic tests for the NPC uplink helpers: prefix matching, npc id
// extraction, and the utterance event envelope. The main.cc interception
// branch is exercised end-to-end by test_services.sh --smoke-npc.
#include <gtest/gtest.h>

#include "proto/chat.pb.h"
#include "proto/server_gateway.pb.h"
#include "npc_uplink.h"

namespace {

using chirp::chat::npc::IsNpcReceiver;
using chirp::chat::npc::MakeUtteranceEvent;
using chirp::chat::npc::NpcIdFromReceiver;

TEST(NpcUplinkTest, IsNpcReceiverMatchesPrefix) {
  EXPECT_TRUE(IsNpcReceiver("npc:", "npc:blacksmith_01"));
  EXPECT_TRUE(IsNpcReceiver("npc:", "npc:"));
  EXPECT_FALSE(IsNpcReceiver("npc:", "blacksmith_01"));
  EXPECT_FALSE(IsNpcReceiver("npc:", "player_1"));
  // Partial or embedded prefixes do not count: matching is head-anchored.
  EXPECT_FALSE(IsNpcReceiver("npc:", "an:npc:x"));
  EXPECT_FALSE(IsNpcReceiver("npc:", "npcx"));
}

TEST(NpcUplinkTest, EmptyPrefixNeverMatches) {
  EXPECT_FALSE(IsNpcReceiver("", "npc:blacksmith_01"));
  EXPECT_FALSE(IsNpcReceiver("", ""));
}

TEST(NpcUplinkTest, EmptyReceiverNeverMatches) {
  EXPECT_FALSE(IsNpcReceiver("npc:", ""));
}

TEST(NpcUplinkTest, NpcIdFromReceiverStripsPrefix) {
  EXPECT_EQ(NpcIdFromReceiver("npc:", "npc:blacksmith_01"), "blacksmith_01");
  EXPECT_EQ(NpcIdFromReceiver("npc:", "npc:"), "");
  EXPECT_EQ(NpcIdFromReceiver("npc:", "player_1"), "");
  EXPECT_EQ(NpcIdFromReceiver("", "npc:x"), "");
}

TEST(NpcUplinkTest, MakeUtteranceEventCarriesMessageFields) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id("m-1");
  msg.set_sender_id("user_2");
  msg.set_receiver_id("npc:blacksmith_01");
  msg.set_channel_type(chirp::chat::PRIVATE);
  msg.set_content("need a sword");
  msg.set_timestamp(1234);

  auto event = MakeUtteranceEvent(msg, "npc:", "npc_dialog");
  EXPECT_EQ(event.event_id(), "m-1");
  EXPECT_EQ(event.target_service_id(), "npc_dialog");
  EXPECT_EQ(event.event_type(), "npc.player_message");

  chirp::chat::NpcPlayerUtterance utterance;
  ASSERT_TRUE(utterance.ParseFromString(event.payload()));
  EXPECT_EQ(utterance.message_id(), "m-1");
  EXPECT_EQ(utterance.sender_id(), "user_2");
  EXPECT_EQ(utterance.npc_id(), "blacksmith_01");
  EXPECT_EQ(utterance.content(), "need a sword");
  EXPECT_EQ(utterance.timestamp(), 1234);
}

}  // namespace
