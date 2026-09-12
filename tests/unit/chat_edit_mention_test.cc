// Tests for the message edit/delete and mention handlers that wire their
// managers into the chat packet dispatch. Channel members, moderator rights
// and delivery are injected as recording lambdas.
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

class EditMentionHandlersTest : public ::testing::Test {
 protected:
  void SetUp() override {
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
    moderator_ = [this](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                        const std::string& user_id) {
      return moderators_.count(channel_id + ":" + user_id) > 0 &&
             channel_type == chirp::chat::GUILD;
    };
    notifier_ = [this](const std::string& user_id, chirp::gateway::MsgID msg_id,
                       const google::protobuf::Message& body) {
      notifications_.push_back({user_id, msg_id, body.SerializeAsString()});
      return true;
    };
    edit_handlers_ = std::make_unique<chirp::chat::MessageEditHandlers>(
        edits_, resolver_, moderator_, notifier_);
    mention_handlers_ =
        std::make_unique<chirp::chat::MentionHandlers>(mentions_, moderator_);
  }

  // Registers a tracked, non-deleted message in both the channel map and the
  // edit manager, as the SEND_MESSAGE path would.
  void RegisterSentMessage(const std::string& message_id, const std::string& sender_id,
                           chirp::chat::ChannelType channel_type,
                           const std::string& channel_id, const std::string& content) {
    edit_handlers_->TrackMessage(message_id, channel_type, channel_id);
    edit_handlers_->RegisterMessage(message_id, sender_id, content);
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
  std::unordered_map<std::string, bool> moderators_;
  std::vector<NotificationRecord> notifications_;
  chirp::chat::ChannelMemberResolver resolver_;
  chirp::chat::ChannelModeratorChecker moderator_;
  chirp::chat::UserNotifier notifier_;

  chirp::chat::MessageEditManager edits_;
  chirp::chat::MentionManager mentions_;

  std::unique_ptr<chirp::chat::MessageEditHandlers> edit_handlers_;
  std::unique_ptr<chirp::chat::MentionHandlers> mention_handlers_;
};

// ---------------------------------------------------------------------------
// Edit message
// ---------------------------------------------------------------------------

TEST_F(EditMentionHandlersTest, EditRejectsMissingFields) {
  chirp::chat::EditMessageRequest no_id;
  no_id.set_user_id("alice");
  no_id.set_new_content("x");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(no_id, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::EditMessageRequest no_content;
  no_content.set_message_id("m1");
  no_content.set_user_id("alice");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(no_content, "alice").code(),
            chirp::common::INVALID_PARAM);
}

TEST_F(EditMentionHandlersTest, EditRejectsIdentityMismatch) {
  chirp::chat::EditMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("mallory");
  req.set_new_content("x");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(req, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, EditUnknownMessageReturnsUserNotFound) {
  chirp::chat::EditMessageRequest req;
  req.set_message_id("ghost");
  req.set_user_id("alice");
  req.set_new_content("x");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(EditMentionHandlersTest, EditByNonSenderNonModeratorFails) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::EditMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("bob");
  req.set_new_content("hacked");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(req, "bob").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, EditBySenderSucceedsAndBroadcasts) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::EditMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  req.set_new_content("hello edited");

  const auto resp = edit_handlers_->HandleEditMessage(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  ASSERT_TRUE(resp.has_message());
  EXPECT_EQ(resp.message().message_id(), "m1");
  EXPECT_EQ(resp.message().content(), "hello edited");
  EXPECT_TRUE(resp.message().is_edited());
  EXPECT_EQ(resp.message().edit_count(), 1);
  EXPECT_GT(resp.server_time(), 0);

  ASSERT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_EDITED_NOTIFY), 1);
  chirp::chat::MessageEditedNotify notify;
  const std::string* body = nullptr;
  for (const auto& record : notifications_) {
    if (record.user_id == "bob") {
      body = &record.body;
    }
  }
  ASSERT_NE(body, nullptr);
  ASSERT_TRUE(notify.ParseFromString(*body));
  EXPECT_EQ(notify.message_id(), "m1");
  EXPECT_EQ(notify.channel_id(), "alice|bob");
  EXPECT_EQ(notify.new_content(), "hello edited");
  EXPECT_EQ(notify.edited_by(), "alice");
}

TEST_F(EditMentionHandlersTest, ModeratorEditsOthersMessageInGroup) {
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m2", "alice", chirp::chat::GUILD, "g1", "hi");

  chirp::chat::EditMessageRequest req;
  req.set_message_id("m2");
  req.set_user_id("carl");
  req.set_new_content("mod edit");
  const auto resp = edit_handlers_->HandleEditMessage(req, "carl");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_EDITED_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::MESSAGE_EDITED_NOTIFY), 1);
}

TEST_F(EditMentionHandlersTest, EditAfterDeleteFails) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest del;
  del.set_message_id("m1");
  del.set_user_id("alice");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(del, "alice").code(), chirp::common::OK);

  chirp::chat::EditMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  req.set_new_content("zombie");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(req, "alice").code(),
            chirp::common::AUTH_FAILED);
}

// ---------------------------------------------------------------------------
// Delete message
// ---------------------------------------------------------------------------

TEST_F(EditMentionHandlersTest, DeleteValidatesInput) {
  chirp::chat::DeleteMessageRequest no_id;
  no_id.set_user_id("alice");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(no_id, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::DeleteMessageRequest spoofed;
  spoofed.set_message_id("m1");
  spoofed.set_user_id("mallory");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(spoofed, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, DeleteUnknownMessageReturnsUserNotFound) {
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("ghost");
  req.set_user_id("alice");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(EditMentionHandlersTest, DeleteByStrangerFails) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("bob");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "bob").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, SoftDeleteBySenderBroadcasts) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");

  const auto resp = edit_handlers_->HandleDeleteMessage(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.was_permanently_deleted());
  EXPECT_GT(resp.server_time(), 0);

  ASSERT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
  chirp::chat::MessageDeletedNotify notify;
  const std::string* body = nullptr;
  for (const auto& record : notifications_) {
    if (record.user_id == "bob") {
      body = &record.body;
    }
  }
  ASSERT_NE(body, nullptr);
  ASSERT_TRUE(notify.ParseFromString(*body));
  EXPECT_EQ(notify.message_id(), "m1");
  EXPECT_FALSE(notify.is_hard_delete());
  EXPECT_EQ(notify.deleted_by(), "alice");
}

TEST_F(EditMentionHandlersTest, HardDeleteRequiresModerator) {
  group_members_ = {"alice", "bob"};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "hi");

  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  req.set_is_hard_delete(true);
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::AUTH_FAILED);

  moderators_ = {{"g1:carl", true}};
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  mod_req.set_is_hard_delete(true);
  const auto resp = edit_handlers_->HandleDeleteMessage(mod_req, "carl");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.was_permanently_deleted());
}

// ---------------------------------------------------------------------------
// Bulk delete
// ---------------------------------------------------------------------------

TEST_F(EditMentionHandlersTest, BulkDeleteValidatesInput) {
  chirp::chat::BulkDeleteRequest no_ids;
  no_ids.set_channel_id("g1");
  no_ids.set_requester_id("alice");
  EXPECT_EQ(edit_handlers_->HandleBulkDelete(no_ids, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::BulkDeleteRequest no_channel;
  no_channel.add_message_ids("m1");
  no_channel.set_requester_id("alice");
  EXPECT_EQ(edit_handlers_->HandleBulkDelete(no_channel, "alice").code(),
            chirp::common::INVALID_PARAM);

  chirp::chat::BulkDeleteRequest spoofed;
  spoofed.add_message_ids("m1");
  spoofed.set_channel_id("g1");
  spoofed.set_requester_id("mallory");
  EXPECT_EQ(edit_handlers_->HandleBulkDelete(spoofed, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, BulkDeleteReportsFailuresAndBroadcastsSuccesses) {
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "a");
  RegisterSentMessage("m2", "bob", chirp::chat::GUILD, "g1", "b");

  // carl (moderator) deletes m1 (ok) and a non-existent m9 (failed).
  chirp::chat::BulkDeleteRequest req;
  req.add_message_ids("m1");
  req.add_message_ids("m9");
  req.set_channel_id("g1");
  req.set_requester_id("carl");

  const auto resp = edit_handlers_->HandleBulkDelete(req, "carl");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.deleted_count(), 1);
  ASSERT_EQ(resp.failed_message_ids_size(), 1);
  EXPECT_EQ(resp.failed_message_ids(0), "m9");
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);

  // Without moderator rights only own messages are deletable.
  chirp::chat::BulkDeleteRequest plain;
  plain.add_message_ids("m2");
  plain.set_channel_id("g1");
  plain.set_requester_id("alice");
  const auto plain_resp = edit_handlers_->HandleBulkDelete(plain, "alice");
  EXPECT_EQ(plain_resp.deleted_count(), 0);
  EXPECT_EQ(plain_resp.failed_message_ids_size(), 1);
}

// ---------------------------------------------------------------------------
// Mentions
// ---------------------------------------------------------------------------

chirp::chat::ChatMessage MakeMessage(const std::string& sender,
                                     const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_sender_id(sender);
  msg.set_content(content);
  msg.set_channel_type(chirp::chat::GUILD);
  msg.set_channel_id("g1");
  return msg;
}

TEST_F(EditMentionHandlersTest, PlainMessagePassesMentionCheck) {
  chirp::common::ErrorCode code = chirp::common::INTERNAL_ERROR;
  EXPECT_TRUE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "hello world"), &code));
  EXPECT_EQ(code, chirp::common::OK);
}

TEST_F(EditMentionHandlersTest, EveryoneMentionIsAllowedOnceThenCooldown) {
  EXPECT_TRUE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "@everyone hi"), nullptr));
  EXPECT_TRUE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("bob", "@everyone hi"), nullptr));

  chirp::common::ErrorCode code = chirp::common::OK;
  EXPECT_FALSE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "@everyone again"), &code));
  EXPECT_EQ(code, chirp::common::AUTH_FAILED);

  // @here shares the same cooldown key.
  EXPECT_FALSE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "@here"), &code));
}

TEST_F(EditMentionHandlersTest, ModeratorBypassesEveryoneCooldown) {
  moderators_ = {{"g1:carl", true}};
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(mention_handlers_->ProcessOutgoingMessage(
        MakeMessage("carl", "@everyone spam"), nullptr));
  }
}

TEST_F(EditMentionHandlersTest, SuggestionsValidateIdentity) {
  chirp::chat::GetMentionSuggestionsRequest spoofed;
  spoofed.set_user_id("mallory");
  EXPECT_EQ(mention_handlers_->HandleGetMentionSuggestions(spoofed, "alice").code(),
            chirp::common::AUTH_FAILED);
}

TEST_F(EditMentionHandlersTest, SuggestionsReturnEntriesForQuery) {
  chirp::chat::GetMentionSuggestionsRequest empty;
  empty.set_user_id("alice");
  const auto all = mention_handlers_->HandleGetMentionSuggestions(empty, "alice");
  EXPECT_EQ(all.code(), chirp::common::OK);
  EXPECT_EQ(all.suggestions_size(), 2);
  EXPECT_EQ(all.suggestions(0).display_text(), "@everyone");
  EXPECT_EQ(all.suggestions(0).type(), chirp::chat::MENTION_TYPE_EVERYONE);
  EXPECT_EQ(all.suggestions(1).display_text(), "@here");

  chirp::chat::GetMentionSuggestionsRequest filtered;
  filtered.set_user_id("alice");
  filtered.set_query("eve");
  const auto some = mention_handlers_->HandleGetMentionSuggestions(filtered, "alice");
  EXPECT_EQ(some.suggestions_size(), 1);
  EXPECT_EQ(some.suggestions(0).display_text(), "@everyone");

  chirp::chat::GetMentionSuggestionsRequest none;
  none.set_user_id("alice");
  none.set_query("zzz");
  EXPECT_EQ(
      mention_handlers_->HandleGetMentionSuggestions(none, "alice").suggestions_size(),
      0);
}

TEST_F(EditMentionHandlersTest, TrackMessageIgnoresEmptyInput) {
  edit_handlers_->TrackMessage("", chirp::chat::PRIVATE, "alice|bob");
  edit_handlers_->TrackMessage("m1", chirp::chat::PRIVATE, "");
  chirp::chat::EditMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  req.set_new_content("x");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

}  // namespace
