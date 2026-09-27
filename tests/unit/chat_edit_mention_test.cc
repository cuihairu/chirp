// Tests for the message edit/delete and mention handlers that wire their
// managers into the chat packet dispatch. Channel members, moderator rights
// and delivery are injected as recording lambdas.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "message_handlers.h"
#include "recall_tombstone.h"

#include "proto/common.pb.h"

namespace {

// 历史存档里的一条消息(带 message_id,内存向量原位改写要按它匹配)。
chirp::chat::ChatMessage MakeTracked(const std::string& message_id,
                                     const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(message_id);
  msg.set_content(content);
  msg.set_timestamp(1000);
  return msg;
}

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
    purger_ = [this](const std::string& message_id, const std::string& receiver_id) {
      purged_.push_back({message_id, receiver_id});
    };
    marker_ = [this](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                     const std::string& message_id) {
      marked_.push_back({channel_type, channel_id, message_id});
    };
    edit_handlers_ = std::make_unique<chirp::chat::MessageEditHandlers>(
        edits_, resolver_, moderator_, notifier_, purger_, marker_);
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

  // (message_id, receiver_id) pairs whose offline copy was reclaimed.
  std::vector<std::pair<std::string, std::string>> PurgedFor(
      const std::string& message_id) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& record : purged_) {
      if (record.first == message_id) {
        out.push_back(record);
      }
    }
    return out;
  }

  size_t MarkedCount() const { return marked_.size(); }

  std::vector<std::string> group_members_;
  std::unordered_map<std::string, bool> moderators_;
  std::vector<NotificationRecord> notifications_;
  std::vector<std::pair<std::string, std::string>> purged_;
  struct MarkRecord {
    chirp::chat::ChannelType channel_type;
    std::string channel_id;
    std::string message_id;
  };
  std::vector<MarkRecord> marked_;
  chirp::chat::ChannelMemberResolver resolver_;
  chirp::chat::ChannelModeratorChecker moderator_;
  chirp::chat::UserNotifier notifier_;
  chirp::chat::OfflineMessagePurger purger_;
  chirp::chat::RecallTombstoneMarker marker_;

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
// Recall (game_chat_features P0 消息撤回)
// ---------------------------------------------------------------------------

TEST_F(EditMentionHandlersTest, SenderRecallBroadcastsTombstone) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");

  const auto resp = edit_handlers_->HandleDeleteMessage(req, "alice");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.was_permanently_deleted());

  // The receiver learns about the withdrawal from the delete notify: soft
  // delete, withdrawn by the author, so the client renders a "recalled"
  // tombstone instead of dropping the bubble silently.
  ASSERT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
  chirp::chat::MessageDeletedNotify notify;
  bool parsed = false;
  for (const auto& record : notifications_) {
    if (record.user_id == "bob") {
      ASSERT_TRUE(notify.ParseFromString(record.body));
      parsed = true;
    }
  }
  ASSERT_TRUE(parsed);
  EXPECT_EQ(notify.message_id(), "m1");
  EXPECT_FALSE(notify.is_hard_delete());
  EXPECT_EQ(notify.deleted_by(), "alice");
  EXPECT_GT(notify.deleted_at(), 0);
}

TEST_F(EditMentionHandlersTest, RecallRejectsSecondAttempt) {
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(), chirp::common::OK);

  // No second broadcast: the client keeps the tombstone it already rendered.
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::INVALID_PARAM);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
  // The archive marker follows the same once-only rule.
  EXPECT_EQ(MarkedCount(), 1u);
}

TEST_F(EditMentionHandlersTest, RecallRejectedOnNonRecallableChannel) {
  RegisterSentMessage("m1", "alice", chirp::chat::WORLD, "world", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::INVALID_PARAM);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 0);
}

TEST_F(EditMentionHandlersTest, RecallOfUntrackedMessageReturnsUserNotFound) {
  // The handler knows the channel (TrackedMessage) but the edit ledger never
  // registered it -- e.g. a message that predates this process. Nothing to
  // withdraw, and the sender must not be told "permission denied".
  edit_handlers_->TrackMessage("m1", chirp::chat::PRIVATE, "alice|bob");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::USER_NOT_FOUND);
}

TEST_F(EditMentionHandlersTest, RecallReclaimsQueuedOfflineCopies) {
  // The private receiver is offline, so the only delivery path left was the
  // queue: the recall must reclaim that copy, not just broadcast.
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(), chirp::common::OK);

  const auto purged = PurgedFor("m1");
  ASSERT_EQ(purged.size(), 1u);
  EXPECT_EQ(purged[0].second, "bob");
}

TEST_F(EditMentionHandlersTest, ModeratorRemovalReclaimsQueuedCopiesToo) {
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "spam");
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(mod_req, "carl").code(),
            chirp::common::OK);

  // Every member except the requester may hold a queued copy.
  const auto purged = PurgedFor("m1");
  ASSERT_EQ(purged.size(), 2u);
  EXPECT_EQ(purged[0].second, "alice");
  EXPECT_EQ(purged[1].second, "bob");
}

TEST_F(EditMentionHandlersTest, RefusedRecallTouchesNoOfflineQueue) {
  RegisterSentMessage("m1", "alice", chirp::chat::WORLD, "world", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  // World channel is not recallable: no notify, nothing is reclaimed, and the
  // history archive is left untouched.
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::INVALID_PARAM);
  EXPECT_TRUE(purged_.empty());
  EXPECT_TRUE(marked_.empty());
}

TEST_F(EditMentionHandlersTest, NullPurgerIsTolerated) {
  // Deployments without an offline queue (or embedders that do not wire one)
  // pass no purger at all; the recall must still succeed.
  chirp::chat::MessageEditHandlers handlers(edits_, resolver_, moderator_, notifier_);
  handlers.TrackMessage("m1", chirp::chat::PRIVATE, "alice|bob");
  handlers.RegisterMessage("m1", "alice", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  EXPECT_EQ(handlers.HandleDeleteMessage(req, "alice").code(), chirp::common::OK);
  EXPECT_TRUE(purged_.empty());
}

TEST_F(EditMentionHandlersTest, ModeratorRemovalOfUntrackedMessageFails) {
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  // Tracked channel but no ledger entry: the manager has nothing to delete.
  edit_handlers_->TrackMessage("m1", chirp::chat::GUILD, "g1");
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(mod_req, "carl").code(),
            chirp::common::AUTH_FAILED);
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::MESSAGE_DELETED_NOTIFY), 0);
}

TEST_F(EditMentionHandlersTest, ModeratorRemovalIgnoresRecallRules) {
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  // carl is not the sender: the recall path would answer kNotSender, so this
  // also pins that moderator removal does not run through the recall rules.
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "spam");
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(mod_req, "carl").code(),
            chirp::common::OK);
  EXPECT_FALSE(mod_req.is_hard_delete());
  EXPECT_EQ(CountNotifications("alice", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
}

TEST_F(EditMentionHandlersTest, RecallMarksHistoryTombstone) {
  // A successful recall hands the tracked channel identity to the archive
  // marker exactly once, so the store can flip is_recalled in place.
  RegisterSentMessage("m1", "alice", chirp::chat::PRIVATE, "alice|bob", "hi");
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(req, "alice").code(),
            chirp::common::OK);

  ASSERT_EQ(marked_.size(), 1u);
  EXPECT_EQ(marked_[0].channel_type, chirp::chat::PRIVATE);
  EXPECT_EQ(marked_[0].channel_id, "alice|bob");
  EXPECT_EQ(marked_[0].message_id, "m1");
}

TEST_F(EditMentionHandlersTest, ModeratorSoftDeleteMarksHistoryTombstone) {
  // Moderator removal defaults to a soft delete: the archive keeps the row and
  // only flips the tombstone.
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "spam");
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(mod_req, "carl").code(),
            chirp::common::OK);

  ASSERT_EQ(marked_.size(), 1u);
  EXPECT_EQ(marked_[0].channel_type, chirp::chat::GUILD);
  EXPECT_EQ(marked_[0].channel_id, "g1");
  EXPECT_EQ(marked_[0].message_id, "m1");
}

TEST_F(EditMentionHandlersTest, HardDeleteSkipsTombstoneMark) {
  // Hard delete is a governance erasure: the row leaves the archive, so there
  // is nothing to mark recalled. Members are still told the message is gone.
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "spam");
  chirp::chat::DeleteMessageRequest mod_req;
  mod_req.set_message_id("m1");
  mod_req.set_user_id("carl");
  mod_req.set_is_hard_delete(true);
  ASSERT_EQ(edit_handlers_->HandleDeleteMessage(mod_req, "carl").code(),
            chirp::common::OK);
  EXPECT_TRUE(marked_.empty());
  EXPECT_EQ(CountNotifications("bob", chirp::gateway::MESSAGE_DELETED_NOTIFY), 1);
}

// ---------------------------------------------------------------------------
// Bulk delete
// ---------------------------------------------------------------------------

TEST_F(EditMentionHandlersTest, BulkDeleteMarksTombstoneAndPurgesPerMessage) {
  // 批量软删走的是和单体软删同一条语义:成功的每条都立墓碑、每个成员都回收
  // 离线副本;失败的那条什么都不做(否则历史里照样躺着原文)。
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  RegisterSentMessage("m1", "alice", chirp::chat::GUILD, "g1", "a");
  RegisterSentMessage("m2", "bob", chirp::chat::GUILD, "g1", "b");

  chirp::chat::BulkDeleteRequest req;
  req.add_message_ids("m1");
  req.add_message_ids("m9");  // 不存在的条目:不得触发墓碑与回收
  req.set_channel_id("g1");
  req.set_requester_id("carl");
  ASSERT_EQ(edit_handlers_->HandleBulkDelete(req, "carl").code(), chirp::common::OK);

  ASSERT_EQ(marked_.size(), 1u);
  EXPECT_EQ(marked_[0].channel_type, chirp::chat::GUILD);
  EXPECT_EQ(marked_[0].channel_id, "g1");
  EXPECT_EQ(marked_[0].message_id, "m1");

  const auto purged = PurgedFor("m1");
  EXPECT_EQ(purged.size(), 2u);  // 除请求者外的每个成员都可能有排队副本
  EXPECT_TRUE(PurgedFor("m9").empty());
}

TEST_F(EditMentionHandlersTest, BulkDeleteToleratesNullMarkerAndPurger) {
  // 部署形态可以不接墓碑/离线队列(4 参构造),批量软删仍须成功。
  group_members_ = {"alice", "bob"};
  moderators_ = {{"g1:carl", true}};
  chirp::chat::MessageEditHandlers handlers(edits_, resolver_, moderator_, notifier_);
  handlers.TrackMessage("m1", chirp::chat::GUILD, "g1");
  handlers.RegisterMessage("m1", "alice", "a");

  chirp::chat::BulkDeleteRequest req;
  req.add_message_ids("m1");
  req.set_channel_id("g1");
  req.set_requester_id("carl");
  EXPECT_EQ(handlers.HandleBulkDelete(req, "carl").code(), chirp::common::OK);
  EXPECT_TRUE(marked_.empty());
  EXPECT_TRUE(purged_.empty());
}

TEST_F(EditMentionHandlersTest, RecallWritesTombstoneIntoRealHistoryArchive) {
  // 端到端语义闭环:撤回 → 墓碑原语落到真实历史向量 → 历史读回只剩墓碑。
  // 这里接的是生产同一份 recall_tombstone 实现(main.cc 的历史向量用的就是它),
  // 而不是只断言 marker 被调用——「不再返回原文」必须是读出来的结果。
  std::vector<chirp::chat::ChatMessage> archive = {MakeTracked("m1", "the secret text")};
  chirp::chat::MessageEditHandlers handlers(
      edits_, resolver_, moderator_, notifier_,
      [](const std::string&, const std::string&) {},
      [&archive](chirp::chat::ChannelType, const std::string&,
                 const std::string& message_id) {
        chirp::chat::MarkRecalledInMemory(archive, message_id);
      });
  handlers.TrackMessage("m1", chirp::chat::PRIVATE, "alice|bob");
  handlers.RegisterMessage("m1", "alice", "the secret text");

  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m1");
  req.set_user_id("alice");
  ASSERT_EQ(handlers.HandleDeleteMessage(req, "alice").code(), chirp::common::OK);

  ASSERT_EQ(archive.size(), 1u);
  EXPECT_TRUE(archive[0].is_recalled());
  EXPECT_TRUE(archive[0].content().empty());
  EXPECT_EQ(archive[0].message_id(), "m1");  // 墓碑仍在原位:分页与序不受影响
}

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

  // The deny path must tolerate a null reason pointer (the gateway always
  // passes one; other embedders may not).
  EXPECT_FALSE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "@everyone again"), nullptr));
  EXPECT_FALSE(mention_handlers_->ProcessOutgoingMessage(
      MakeMessage("alice", "@here"), nullptr));
}

TEST_F(EditMentionHandlersTest, SameUserRejectsEmptyClaimedIds) {
  // Empty claimed ids fail the !claimed.empty() short-circuit before the
  // equality compare (spoof tests only cover non-empty mismatches).
  chirp::chat::EditMessageRequest edit;
  edit.set_message_id("msg_e");
  edit.set_new_content("x");
  EXPECT_EQ(edit_handlers_->HandleEditMessage(edit, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::DeleteMessageRequest del;
  del.set_message_id("msg_e");
  EXPECT_EQ(edit_handlers_->HandleDeleteMessage(del, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::BulkDeleteRequest bulk;
  bulk.set_channel_id("ch1");
  bulk.add_message_ids("msg_e");
  EXPECT_EQ(edit_handlers_->HandleBulkDelete(bulk, "alice").code(),
            chirp::common::AUTH_FAILED);

  chirp::chat::GetMentionSuggestionsRequest sugg;
  sugg.set_user_id("");
  EXPECT_EQ(mention_handlers_->HandleGetMentionSuggestions(sugg, "alice").code(),
            chirp::common::AUTH_FAILED);
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
