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

}  // namespace chirp::chat

#endif  // CHIRP_CHAT_MESSAGE_HANDLERS_H_
