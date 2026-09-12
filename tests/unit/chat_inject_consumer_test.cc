// Unit tests for the chat-side injection consumer: hub-forwarded
// INJECT_MESSAGE_NOTIFY payloads must pass validation and then flow through
// the same store/deliver/queue tail as SEND_MESSAGE.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "inject_consumer.h"

namespace {

using chirp::chat::ChatMessage;
using chirp::chat::InjectConsumer;
using chirp::chat::InjectHooks;
using chirp::chat::InjectOutcome;
using chirp::common::ErrorCode;
using chirp::server_gateway::InjectMessageNotify;

// Records every hook invocation so tests can assert the delivery tail.
struct RecordingHooks {
  std::vector<ChatMessage> stored;
  std::vector<std::string> private_channel_calls;
  std::vector<std::string> delivered;
  std::vector<std::string> queued;
  std::vector<std::string> broadcast_calls;
  std::vector<std::string> broadcast_offline;
  bool receiver_online = true;

  InjectHooks MakeHooks() {
    InjectHooks hooks;
    hooks.private_channel_id =
        [this](const std::string& a, const std::string& b) {
          private_channel_calls.push_back(a + "|" + b);
          // Canonical pair: sorted, matching MessageStore::PrivateChannelId.
          return std::min(a, b) + "|" + std::max(a, b);
        };
    hooks.store_message = [this](const ChatMessage& msg) { stored.push_back(msg); };
    hooks.deliver_private =
        [this](const std::string& receiver_id, const ChatMessage&) {
          if (!receiver_online) {
            return false;
          }
          delivered.push_back(receiver_id);
          return true;
        };
    hooks.queue_offline =
        [this](const std::string& user_id, const ChatMessage&) { queued.push_back(user_id); };
    hooks.broadcast_channel =
        [this](const std::string& channel_id, const ChatMessage&) {
          broadcast_calls.push_back(channel_id);
          return broadcast_offline;
        };
    return hooks;
  }
};

InjectMessageNotify MakeNotify(const std::string& sender_id, const std::string& content) {
  InjectMessageNotify notify;
  auto* req = notify.mutable_message();
  req->set_inject_id("inj-1");
  req->set_sender_kind(chirp::server_gateway::SENDER_NPC);
  req->set_sender_id(sender_id);
  req->set_channel_type(static_cast<int32_t>(chirp::chat::PRIVATE));
  req->set_receiver_id("player_1");
  req->set_content(content);
  return notify;
}

TEST(InjectConsumerTest, DeliversPrivateToOnlineReceiver) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("npc:blacksmith_01", "hello"));

  EXPECT_EQ(out.code, ErrorCode::OK);
  EXPECT_FALSE(out.message_id.empty());
  ASSERT_EQ(rec.stored.size(), 1u);
  const ChatMessage& msg = rec.stored[0];
  EXPECT_EQ(msg.message_id(), out.message_id);
  EXPECT_EQ(msg.sender_id(), "npc:blacksmith_01");
  EXPECT_EQ(msg.receiver_id(), "player_1");
  EXPECT_EQ(msg.channel_type(), chirp::chat::PRIVATE);
  EXPECT_EQ(msg.channel_id(), "npc:blacksmith_01|player_1");
  EXPECT_EQ(msg.content(), "hello");
  EXPECT_GT(msg.timestamp(), 0);
  EXPECT_EQ(msg.msg_type(), chirp::chat::TEXT);
  ASSERT_EQ(rec.delivered.size(), 1u);
  EXPECT_EQ(rec.delivered[0], "player_1");
  EXPECT_TRUE(rec.queued.empty());
  EXPECT_TRUE(rec.broadcast_calls.empty());
}

TEST(InjectConsumerTest, QueuesPrivateWhenReceiverOffline) {
  RecordingHooks rec;
  rec.receiver_online = false;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("system", "maintenance"));

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_TRUE(rec.delivered.empty());
  ASSERT_EQ(rec.queued.size(), 1u);
  EXPECT_EQ(rec.queued[0], "player_1");
}

TEST(InjectConsumerTest, BroadcastsChannelAndQueuesOfflineMembers) {
  RecordingHooks rec;
  rec.broadcast_offline = {"member_2", "member_3"};
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("trade", "auction open");
  auto* req = notify.mutable_message();
  req->set_sender_kind(chirp::server_gateway::SENDER_SERVICE);
  req->set_channel_type(static_cast<int32_t>(chirp::chat::GUILD));
  req->clear_receiver_id();
  req->set_channel_id("guild_7");

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_EQ(rec.stored[0].channel_id(), "guild_7");
  EXPECT_EQ(rec.stored[0].channel_type(), chirp::chat::GUILD);
  ASSERT_EQ(rec.broadcast_calls.size(), 1u);
  EXPECT_EQ(rec.broadcast_calls[0], "guild_7");
  EXPECT_TRUE(rec.delivered.empty());
  ASSERT_EQ(rec.queued.size(), 2u);
  EXPECT_EQ(rec.queued[0], "member_2");
  EXPECT_EQ(rec.queued[1], "member_3");
}

TEST(InjectConsumerTest, BroadcastWithoutMembersStoresOnly) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "announcement");
  auto* req = notify.mutable_message();
  req->set_channel_type(static_cast<int32_t>(chirp::chat::WORLD));
  req->clear_receiver_id();
  req->set_channel_id("world");

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_TRUE(rec.queued.empty());
}

TEST(InjectConsumerTest, RejectsEmptyContent) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("npc", ""));

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(out.message_id.empty());
  EXPECT_TRUE(rec.stored.empty());
  EXPECT_TRUE(rec.delivered.empty());
  EXPECT_TRUE(rec.queued.empty());
}

TEST(InjectConsumerTest, RejectsUnknownSenderKind) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("npc", "hello");
  notify.mutable_message()->set_sender_kind(chirp::server_gateway::SENDER_UNKNOWN);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsEmptySenderId) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("", "hello"));

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsInvalidChannelType) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->set_channel_type(42);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsPrivateWithoutReceiver) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->clear_receiver_id();

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsChannelWithoutChannelId) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->set_channel_type(static_cast<int32_t>(chirp::chat::TEAM));
  notify.mutable_message()->clear_receiver_id();

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsEmptyNotify) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(InjectMessageNotify{});

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

}  // namespace
