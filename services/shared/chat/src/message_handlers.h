#ifndef CHIRP_CHAT_MESSAGE_HANDLERS_H_
#define CHIRP_CHAT_MESSAGE_HANDLERS_H_

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <google/protobuf/message.h>

#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "read_receipt_manager.h"
#include "reaction_manager.h"
#include "typing_manager.h"
#include "message_edit_manager.h"
#include "mention_manager.h"

namespace chirp::chat {

// Delivers one notification packet to a single user. Returns true when the
// user is online and the payload was handed to its session; false means the
// user has no live session and the payload is dropped (read receipts,
// typing state and reactions are not queued for offline replay).
using UserNotifier = std::function<bool(
    const std::string& user_id, chirp::gateway::MsgID msg_id,
    const google::protobuf::Message& body)>;

// Resolves the members of a channel that should receive a broadcast,
// excluding the originator. For private channels this is the other party
// (parsed from the canonical "a|b" channel id); for group-style channels
// it is the group membership.
using ChannelMemberResolver = std::function<std::vector<std::string>(
    chirp::chat::ChannelType channel_type, const std::string& channel_id,
    const std::string& exclude_user_id)>;

// Returns true when user_id holds a moderator-or-above role in the channel.
// Group-style channels consult the group membership; private channels have
// no moderators (always false).
using ChannelModeratorChecker = std::function<bool(
    chirp::chat::ChannelType channel_type, const std::string& channel_id,
    const std::string& user_id)>;

// Drops a message that is still sitting in a recipient's offline queue.
// Wired to the message store's offline queue on the basic form; left unset
// (or set to a no-op) on deployments without an offline queue.
using OfflineMessagePurger = std::function<void(
    const std::string& message_id, const std::string& receiver_id)>;

// Handlers for the read-receipt message ids (MARK_READ, GET_READ_RECEIPTS,
// GET_UNREAD_COUNT). Marking a channel read notifies the other channel
// members with MESSAGE_READ_NOTIFY.
class ReadReceiptHandlers {
 public:
  ReadReceiptHandlers(ReadReceiptManager& receipts, ChannelMemberResolver members,
                      UserNotifier notify);

  // Remember which channel a message belongs to (delegates to the manager),
  // so receipts of a message_id-only lookup can resolve the channel.
  void TrackMessage(const std::string& message_id, chirp::chat::ChannelType channel_type,
                    const std::string& channel_id) {
    receipts_.TrackMessage(message_id, channel_id, channel_type);
  }

  chirp::chat::MarkReadResponse HandleMarkRead(
      const chirp::chat::MarkReadRequest& req, std::string_view authenticated_user_id);

  chirp::chat::GetReadReceiptsResponse HandleGetReadReceipts(
      const chirp::chat::GetReadReceiptsRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::GetUnreadCountResponse HandleGetUnreadCount(
      const chirp::chat::GetUnreadCountRequest& req,
      std::string_view authenticated_user_id);

 private:
  ReadReceiptManager& receipts_;
  ChannelMemberResolver members_;
  UserNotifier notify_;
};

// Handlers for typing indicators. The client sends TYPING_INDICATOR_NOTIFY
// with a TypingIndicator body to start/stop typing; the handler updates the
// manager state and, when the manager says the state change should be seen,
// broadcasts the indicator to the other channel members.
class TypingHandlers {
 public:
  TypingHandlers(TypingManager& typing, ChannelMemberResolver members,
                 UserNotifier notify);

  // Returns true when the indicator was broadcast.
  bool HandleTypingIndicator(const chirp::chat::TypingIndicator& indicator,
                             std::string_view authenticated_user_id);

  chirp::chat::GetTypingUsersResponse HandleGetTypingUsers(
      const chirp::chat::GetTypingUsersRequest& req,
      std::string_view authenticated_user_id);

 private:
  TypingManager& typing_;
  ChannelMemberResolver members_;
  UserNotifier notify_;
};

// Handlers for message reactions. Requests carry only a message_id, so the
// handlers keep a message -> channel map (fed via TrackMessage from the
// SEND_MESSAGE path) to know where to broadcast ReactionAdded/RemovedNotify.
class ReactionHandlers {
 public:
  ReactionHandlers(ReactionManager& reactions, ChannelMemberResolver members,
                   UserNotifier notify);

  // Remember which channel a message belongs to (called when the message
  // is accepted for delivery).
  void TrackMessage(const std::string& message_id,
                    chirp::chat::ChannelType channel_type,
                    const std::string& channel_id);

  chirp::chat::AddReactionResponse HandleAddReaction(
      const chirp::chat::AddReactionRequest& req, std::string_view authenticated_user_id);

  chirp::chat::RemoveReactionResponse HandleRemoveReaction(
      const chirp::chat::RemoveReactionRequest& req, std::string_view authenticated_user_id);

  chirp::chat::GetReactionsResponse HandleGetReactions(
      const chirp::chat::GetReactionsRequest& req, std::string_view authenticated_user_id);

 private:
  // Returns the channel of the message, or false when unknown.
  bool ChannelOfMessage(const std::string& message_id,
                        chirp::chat::ChannelType* channel_type,
                        std::string* channel_id);

  void BroadcastReaction(chirp::gateway::MsgID msg_id,
                         const std::string& message_id,
                         const std::string& channel_id,
                         chirp::chat::ChannelType channel_type,
                         const std::string& emoji,
                         const std::string& user_id);

  ReactionManager& reactions_;
  ChannelMemberResolver members_;
  UserNotifier notify_;

  std::mutex mu_;
  struct MessageChannel {
    chirp::chat::ChannelType channel_type;
    std::string channel_id;
  };
  std::unordered_map<std::string, MessageChannel> message_channels_;
};

// Handlers for message edit/delete (EDIT_MESSAGE, DELETE_MESSAGE, BULK_DELETE).
// Requests carry only a message_id, so the handlers keep a message -> channel
// map (fed via TrackMessage from the SEND_MESSAGE path) to know where to
// broadcast MessageEditedNotify / MessageDeletedNotify and where moderator
// rights apply.
class MessageEditHandlers {
 public:
  MessageEditHandlers(MessageEditManager& edits, ChannelMemberResolver members,
                      ChannelModeratorChecker is_moderator, UserNotifier notify,
                      OfflineMessagePurger purge_offline = nullptr);

  // Remember which channel a message belongs to (called when the message
  // is accepted for delivery).
  void TrackMessage(const std::string& message_id,
                    chirp::chat::ChannelType channel_type,
                    const std::string& channel_id);

  // Register the message for edit tracking (delegates to the manager).
  void RegisterMessage(const std::string& message_id, const std::string& sender_id,
                       const std::string& content) {
    edits_.RegisterMessage(message_id, sender_id, content);
  }

  chirp::chat::EditMessageResponse HandleEditMessage(
      const chirp::chat::EditMessageRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::DeleteMessageResponse HandleDeleteMessage(
      const chirp::chat::DeleteMessageRequest& req,
      std::string_view authenticated_user_id);

  chirp::chat::BulkDeleteResponse HandleBulkDelete(
      const chirp::chat::BulkDeleteRequest& req,
      std::string_view authenticated_user_id);

 private:
  // Returns the channel of the message, or false when unknown.
  bool ChannelOfMessage(const std::string& message_id,
                        chirp::chat::ChannelType* channel_type,
                        std::string* channel_id);

  MessageEditManager& edits_;
  ChannelMemberResolver members_;
  ChannelModeratorChecker is_moderator_;
  UserNotifier notify_;
  OfflineMessagePurger purge_offline_;

  std::mutex mu_;
  std::unordered_map<std::string, std::pair<chirp::chat::ChannelType, std::string>>
      message_channels_;
};

// Handlers for mentions. The SEND_MESSAGE path calls ProcessOutgoingMessage to
// parse mentions and enforce the @everyone/@here cooldown; clients query
// autocomplete entries via GET_MENTION_SUGGESTIONS.
class MentionHandlers {
 public:
  MentionHandlers(MentionManager& mentions, ChannelModeratorChecker is_moderator);

  // Parses msg.content() and enforces mention permissions. Returns false with
  // reason set when the message must be rejected (@everyone/@here cooldown).
  bool ProcessOutgoingMessage(const chirp::chat::ChatMessage& msg,
                              chirp::common::ErrorCode* reason);

  chirp::chat::GetMentionSuggestionsResponse HandleGetMentionSuggestions(
      const chirp::chat::GetMentionSuggestionsRequest& req,
      std::string_view authenticated_user_id);

 private:
  MentionManager& mentions_;
  ChannelModeratorChecker is_moderator_;
};

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_MESSAGE_HANDLERS_H_
