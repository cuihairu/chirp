// Tests for the read-receipt / typing / reaction handlers that wire their
// managers into the chat packet dispatch. Channel members and delivery are
// injected as recording lambdas, so no network machinery is involved.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "message_handlers.h"

#include "proto/common.pb.h"

namespace {

struct NotificationRecord {
  std::string user_id;
  chirp::gateway::MsgID msg_id;
  std::string body;
};

class MessageHandlersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Default resolver: private channels resolve to the other party.
    resolver_ = [this](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                       const std::string& exclude_user_id) -> std::vector<std::string> {
      if (channel_type != chirp::chat::PRIVATE) {
        std::vector<std::string> others;
        for (const auto& member : group_members_) {
          if (member != exclude_user_id) {
            others.push_back(member);
          }
        }
        return others;
      }
      const size_t sep = channel_id.find('|');
      if (sep == std::string::npos) {
        return {};
      }
      const std::string left = channel_id.substr(0, sep);
      const std::string right = channel_id.substr(sep + 1);
      const std::string& other = (left == exclude_user_id) ? right : left;
      if (other.empty() || other == exclude_user_id) {
        return {};
      }
      return {other};
    };
    notifier_ = [this](const std::string& user_id, chirp::gateway::MsgID msg_id,
                       const google::protobuf::Message& body) {
      notifications_.push_back({user_id, msg_id, body.SerializeAsString()});
      return true;
    };
    receipt_handlers_ =
        std::make_unique<chirp::chat::ReadReceiptHandlers>(receipts_, resolver_, notifier_);
    typing_handlers_ = std::make_unique<chirp::chat::TypingHandlers>(typing_, resolver_, notifier_);
    reaction_handlers_ =
        std::make_unique<chirp::chat::ReactionHandlers>(reactions_, resolver_, notifier_);
  }

  int CountNotifications(const std::string& user_id, chirp::gateway::MsgID msg_id) const {
    int count = 0;
    for (const auto& record : notifications_) {
      if (record.user_id == user_id && record.msg_id == msg_id) {
        ++count;
      }
    }
    return count;
  }

  std::vector<std::string> group_members_;
  std::vector<NotificationRecord> notifications_;
  chirp::chat::ChannelMemberResolver resolver_;
  chirp::chat::UserNotifier notifier_;

  chirp::chat::ReadReceiptManager receipts_;
  chirp::chat::TypingManager typing_;
  chirp::chat::ReactionManager reactions_;

  std::unique_ptr<chirp::chat::ReadReceiptHandlers> receipt_handlers_;
  std::unique_ptr<chirp::chat::TypingHandlers> typing_handlers_;
  std::unique_ptr<chirp::chat::ReactionHandlers> reaction_handlers_;
};

// ---------------------------------------------------------------------------
// Read receipts
// ---------------------------------------------------------------------------

TEST_F(MessageHandlersTest, MarkReadRejectsEmptyChannel) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id("alice");
  const auto resp = receipt_handlers_->HandleMarkRead(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(MessageHandlersTest, MarkReadRejectsIdentityMismatch) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id("mallory");
  req.set_channel_id("alice|bob");
  const auto resp = receipt_handlers_->HandleMarkRead(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(MessageHandlersTest, MarkReadNotifiesOtherParty) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id("alice");
  req.set_channel_id("alice|bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_message_id("msg_1");
  req.set_read_timestamp(1234);

  const auto resp = receipt_handlers_->HandleMarkRead(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_GT(resp.server_time(), 0);

  ASSERT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_READ_NOTIFY), 1);
  chirp::chat::MessageReadNotify notify;
  const std::string* body = nullptr;
  for (const auto& record : notifications_) {
    if (record.user_id == "bob") {
      body = &record.body;
    }
  }
  ASSERT_NE(body, nullptr);
  ASSERT_TRUE(notify.ParseFromString(*body));
  EXPECT_EQ(notify.message_id(), "msg_1");
  EXPECT_EQ(notify.reader_user_id(), "alice");
  EXPECT_EQ(notify.read_at(), 1234);
}

TEST_F(MessageHandlersTest, MarkReadDerivesTimestampWhenMissing) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id("alice");
  req.set_channel_id("alice|bob");
  req.set_message_id("msg_2");
  const auto resp = receipt_handlers_->HandleMarkRead(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);

  const auto receipts = receipts_.GetReadReceipts("msg_2");
  ASSERT_EQ(receipts.size(), 1u);
  EXPECT_GT(receipts[0].read_at(), 0);
}

TEST_F(MessageHandlersTest, GetReadReceiptsRejectsEmptyMessageId) {
  chirp::chat::GetReadReceiptsRequest req;
  const auto resp = receipt_handlers_->HandleGetReadReceipts(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(MessageHandlersTest, GetReadReceiptsAggregatesReaders) {
  chirp::chat::MarkReadRequest base;
  base.set_channel_id("alice|bob");
  base.set_channel_type(chirp::chat::PRIVATE);
  base.set_message_id("msg_3");

  chirp::chat::MarkReadRequest from_alice = base;
  from_alice.set_user_id("alice");
  EXPECT_EQ(receipt_handlers_->HandleMarkRead(from_alice, "alice").code(), chirp::common::OK);

  chirp::chat::MarkReadRequest from_bob = base;
  from_bob.set_user_id("bob");
  EXPECT_EQ(receipt_handlers_->HandleMarkRead(from_bob, "bob").code(), chirp::common::OK);

  chirp::chat::GetReadReceiptsRequest query;
  query.set_message_id("msg_3");
  const auto resp = receipt_handlers_->HandleGetReadReceipts(query, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.receipts_size(), 2);
}

TEST_F(MessageHandlersTest, GetUnreadCountRejectsIdentityMismatch) {
  chirp::chat::GetUnreadCountRequest req;
  req.set_user_id("mallory");
  const auto resp = receipt_handlers_->HandleGetUnreadCount(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(MessageHandlersTest, GetUnreadCountReturnsCursorChannels) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id("alice");
  req.set_channel_id("alice|bob");
  req.set_message_id("msg_4");
  ASSERT_EQ(receipt_handlers_->HandleMarkRead(req, "alice").code(), chirp::common::OK);

  chirp::chat::GetUnreadCountRequest query;
  query.set_user_id("alice");
  const auto resp = receipt_handlers_->HandleGetUnreadCount(query, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.channels_size(), 1);
  EXPECT_EQ(resp.total_unread(), 0);  // cursors carry no pending unread
}

// ---------------------------------------------------------------------------
// Typing indicators
// ---------------------------------------------------------------------------

TEST_F(MessageHandlersTest, TypingRejectsEmptyChannelOrSpoofedUser) {
  chirp::chat::TypingIndicator empty_channel;
  empty_channel.set_user_id("alice");
  EXPECT_FALSE(typing_handlers_->HandleTypingIndicator(empty_channel, "alice"));

  chirp::chat::TypingIndicator spoofed;
  spoofed.set_channel_id("alice|bob");
  spoofed.set_user_id("mallory");
  EXPECT_FALSE(typing_handlers_->HandleTypingIndicator(spoofed, "alice"));
}

TEST_F(MessageHandlersTest, TypingStartBroadcastsIndicator) {
  chirp::chat::TypingIndicator indicator;
  indicator.set_channel_id("alice|bob");
  indicator.set_channel_type(chirp::chat::PRIVATE);
  indicator.set_user_id("alice");
  indicator.set_username("Alice");
  indicator.set_is_typing(true);

  EXPECT_TRUE(typing_handlers_->HandleTypingIndicator(indicator, "alice"));
  ASSERT_EQ(CountNotifications("bob", chirp::gateway::TYPING_INDICATOR_NOTIFY), 1);

  chirp::chat::GetTypingUsersRequest query;
  query.set_channel_id("alice|bob");
  query.set_channel_type(chirp::chat::PRIVATE);
  const auto resp = typing_handlers_->HandleGetTypingUsers(query, "bob");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.typing_user_ids_size(), 1);
  EXPECT_EQ(resp.typing_user_ids(0), "alice");
}

TEST_F(MessageHandlersTest, TypingCooldownSuppressionPropagates) {
  chirp::chat::TypingIndicator indicator;
  indicator.set_channel_id("alice|bob");
  indicator.set_user_id("alice");
  indicator.set_is_typing(true);
  // The manager force-broadcasts the first start and, by design, the first
  // repeat; the second repeat inside the cooldown is suppressed and the
  // handler must not broadcast it.
  ASSERT_TRUE(typing_handlers_->HandleTypingIndicator(indicator, "alice"));
  ASSERT_TRUE(typing_handlers_->HandleTypingIndicator(indicator, "alice"));
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::TYPING_INDICATOR_NOTIFY), 2);

  EXPECT_FALSE(typing_handlers_->HandleTypingIndicator(indicator, "alice"));
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::TYPING_INDICATOR_NOTIFY), 2);
}

TEST_F(MessageHandlersTest, TypingStopAlwaysBroadcasts) {
  chirp::chat::TypingIndicator start;
  start.set_channel_id("alice|bob");
  start.set_user_id("alice");
  start.set_is_typing(true);
  ASSERT_TRUE(typing_handlers_->HandleTypingIndicator(start, "alice"));

  chirp::chat::TypingIndicator stop = start;
  stop.set_is_typing(false);
  EXPECT_TRUE(typing_handlers_->HandleTypingIndicator(stop, "alice"));
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::TYPING_INDICATOR_NOTIFY), 2);

  chirp::chat::GetTypingUsersRequest query;
  query.set_channel_id("alice|bob");
  const auto resp = typing_handlers_->HandleGetTypingUsers(query, "bob");
  EXPECT_EQ(resp.typing_user_ids_size(), 0);
}

TEST_F(MessageHandlersTest, GetTypingUsersRejectsEmptyChannel) {
  chirp::chat::GetTypingUsersRequest req;
  const auto resp = typing_handlers_->HandleGetTypingUsers(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

// ---------------------------------------------------------------------------
// Reactions
// ---------------------------------------------------------------------------

TEST_F(MessageHandlersTest, AddReactionRejectsMissingFields) {
  chirp::chat::AddReactionRequest no_message;
  no_message.set_user_id("alice");
  no_message.set_emoji("👍");
  EXPECT_EQ(reaction_handlers_->HandleAddReaction(no_message, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::AddReactionRequest no_emoji;
  no_emoji.set_user_id("alice");
  no_emoji.set_message_id("msg_1");
  EXPECT_EQ(reaction_handlers_->HandleAddReaction(no_emoji, "alice").code(),
            chirp::common::INVALID_PARAM);
}

TEST_F(MessageHandlersTest, AddReactionRejectsIdentityMismatch) {
  chirp::chat::AddReactionRequest req;
  req.set_user_id("mallory");
  req.set_message_id("msg_1");
  req.set_emoji("👍");
  EXPECT_EQ(reaction_handlers_->HandleAddReaction(req, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(MessageHandlersTest, AddReactionWithoutKnownChannelSkipsBroadcast) {
  chirp::chat::AddReactionRequest req;
  req.set_user_id("alice");
  req.set_message_id("unknown_msg");
  req.set_emoji("👍");
  const auto resp = reaction_handlers_->HandleAddReaction(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.reaction().count(), 1);
  EXPECT_TRUE(notifications_.empty());
}

TEST_F(MessageHandlersTest, AddReactionBroadcastsToChannelMembers) {
  reaction_handlers_->TrackMessage("msg_1", chirp::chat::PRIVATE, "alice|bob");

  chirp::chat::AddReactionRequest req;
  req.set_user_id("alice");
  req.set_message_id("msg_1");
  req.set_emoji("👍");
  const auto resp = reaction_handlers_->HandleAddReaction(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.reaction().message_id(), "msg_1");
  EXPECT_EQ(resp.reaction().emoji(), "👍");
  EXPECT_EQ(resp.reaction().count(), 1);
  EXPECT_EQ(resp.reaction().user_ids_size(), 1);

  ASSERT_EQ(CountNotifications("bob", chirp::gateway::REACTION_ADDED_NOTIFY), 1);
  chirp::chat::ReactionAddedNotify notify;
  const std::string* body = nullptr;
  for (const auto& record : notifications_) {
    if (record.user_id == "bob") {
      body = &record.body;
    }
  }
  ASSERT_NE(body, nullptr);
  ASSERT_TRUE(notify.ParseFromString(*body));
  EXPECT_EQ(notify.channel_id(), "alice|bob");
  EXPECT_EQ(notify.emoji(), "👍");
  EXPECT_EQ(notify.user_id(), "alice");
}

TEST_F(MessageHandlersTest, AddReactionBroadcastsToGroupMembers) {
  group_members_ = {"alice", "bob", "carl"};
  reaction_handlers_->TrackMessage("msg_g", chirp::chat::GUILD, "g1");

  chirp::chat::AddReactionRequest req;
  req.set_user_id("alice");
  req.set_message_id("msg_g");
  req.set_emoji("🎉");
  EXPECT_EQ(reaction_handlers_->HandleAddReaction(req, "alice").code(), chirp::common::OK);

  EXPECT_EQ(CountNotifications("bob", chirp::gateway::REACTION_ADDED_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("carl", chirp::gateway::REACTION_ADDED_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::REACTION_ADDED_NOTIFY), 0);
}

TEST_F(MessageHandlersTest, RemoveReactionValidatesInput) {
  chirp::chat::RemoveReactionRequest no_fields;
  EXPECT_EQ(reaction_handlers_->HandleRemoveReaction(no_fields, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::RemoveReactionRequest spoofed;
  spoofed.set_user_id("mallory");
  spoofed.set_message_id("msg_1");
  spoofed.set_emoji("👍");
  EXPECT_EQ(reaction_handlers_->HandleRemoveReaction(spoofed, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(MessageHandlersTest, RemoveReactionFailsWhenNotReacted) {
  chirp::chat::RemoveReactionRequest req;
  req.set_user_id("alice");
  req.set_message_id("msg_1");
  req.set_emoji("👍");
  EXPECT_EQ(reaction_handlers_->HandleRemoveReaction(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(MessageHandlersTest, RemoveReactionSucceedsAndBroadcasts) {
  reaction_handlers_->TrackMessage("msg_1", chirp::chat::PRIVATE, "alice|bob");
  chirp::chat::AddReactionRequest add;
  add.set_user_id("alice");
  add.set_message_id("msg_1");
  add.set_emoji("👍");
  ASSERT_EQ(reaction_handlers_->HandleAddReaction(add, "alice").code(), chirp::common::OK);
  notifications_.clear();

  chirp::chat::RemoveReactionRequest remove;
  remove.set_user_id("alice");
  remove.set_message_id("msg_1");
  remove.set_emoji("👍");
  const auto resp = reaction_handlers_->HandleRemoveReaction(remove, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_GT(resp.server_time(), 0);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::REACTION_REMOVED_NOTIFY), 1);
}

TEST_F(MessageHandlersTest, GetReactionsValidatesInput) {
  chirp::chat::GetReactionsRequest empty;
  EXPECT_EQ(reaction_handlers_->HandleGetReactions(empty, "alice").code(),
            chirp::common::INVALID_PARAM);
}

TEST_F(MessageHandlersTest, GetReactionsReturnsAllOrFiltered) {
  chirp::chat::AddReactionRequest base;
  base.set_user_id("alice");
  base.set_message_id("msg_1");

  chirp::chat::AddReactionRequest thumb = base;
  thumb.set_emoji("👍");
  ASSERT_EQ(reaction_handlers_->HandleAddReaction(thumb, "alice").code(), chirp::common::OK);

  chirp::chat::AddReactionRequest heart = base;
  heart.set_emoji("❤️");
  ASSERT_EQ(reaction_handlers_->HandleAddReaction(heart, "alice").code(), chirp::common::OK);

  chirp::chat::GetReactionsRequest all;
  all.set_message_id("msg_1");
  const auto all_resp = reaction_handlers_->HandleGetReactions(all, "alice");
  EXPECT_EQ(all_resp.code(), chirp::common::OK);
  EXPECT_EQ(all_resp.reactions_size(), 2);

  chirp::chat::GetReactionsRequest filtered;
  filtered.set_message_id("msg_1");
  filtered.set_emoji("❤️");
  const auto filtered_resp = reaction_handlers_->HandleGetReactions(filtered, "alice");
  EXPECT_EQ(filtered_resp.reactions_size(), 1);
  EXPECT_EQ(filtered_resp.reactions(0).emoji(), "❤️");

  chirp::chat::GetReactionsRequest unknown;
  unknown.set_message_id("msg_1");
  unknown.set_emoji("🐸");
  EXPECT_EQ(reaction_handlers_->HandleGetReactions(unknown, "alice").reactions_size(), 0);
}

TEST_F(MessageHandlersTest, TrackMessageIgnoresEmptyInput) {
  reaction_handlers_->TrackMessage("", chirp::chat::PRIVATE, "alice|bob");
  reaction_handlers_->TrackMessage("msg_1", chirp::chat::PRIVATE, "");
  // Nothing to assert beyond "no crash"; the map stays empty.
  chirp::chat::AddReactionRequest req;
  req.set_user_id("alice");
  req.set_message_id("msg_1");
  req.set_emoji("👍");
  EXPECT_EQ(reaction_handlers_->HandleAddReaction(req, "alice").code(), chirp::common::OK);
  EXPECT_TRUE(notifications_.empty());
}

}  // namespace
