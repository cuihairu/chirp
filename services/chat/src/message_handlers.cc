#include "message_handlers.h"

#include <algorithm>

#include "runtime_utils.h"

namespace chirp {
namespace chat {

namespace {

bool SameUser(std::string_view authenticated_user_id, const std::string& claimed) {
  return !claimed.empty() && claimed == authenticated_user_id;
}

}  // namespace

// ---------------------------------------------------------------------------
// Read receipts
// ---------------------------------------------------------------------------

ReadReceiptHandlers::ReadReceiptHandlers(ReadReceiptManager& receipts,
                                         ChannelMemberResolver members,
                                         UserNotifier notify)
    : receipts_(receipts), members_(std::move(members)), notify_(std::move(notify)) {}

chirp::chat::MarkReadResponse ReadReceiptHandlers::HandleMarkRead(
    const chirp::chat::MarkReadRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::MarkReadResponse resp;

  if (req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  const int64_t read_at = req.read_timestamp() > 0 ? req.read_timestamp()
                                                   : chirp::chat::runtime::NowMs();
  receipts_.MarkRead(req.user_id(), req.channel_id(), req.channel_type(),
                     req.message_id(), read_at);

  // Tell the other channel members that this user read up to the message.
  chirp::chat::MessageReadNotify notify;
  notify.set_channel_id(req.channel_id());
  notify.set_channel_type(req.channel_type());
  notify.set_message_id(req.message_id());
  notify.set_reader_user_id(req.user_id());
  notify.set_read_at(read_at);
  for (const auto& member : members_(req.channel_type(), req.channel_id(), req.user_id())) {
    notify_(member, chirp::gateway::MESSAGE_READ_NOTIFY, notify);
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(chirp::chat::runtime::NowMs());
  return resp;
}

chirp::chat::GetReadReceiptsResponse ReadReceiptHandlers::HandleGetReadReceipts(
    const chirp::chat::GetReadReceiptsRequest& req, std::string_view /*authenticated_user_id*/) {
  chirp::chat::GetReadReceiptsResponse resp;
  if (req.message_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  for (auto& receipt : receipts_.GetReadReceipts(req.message_id())) {
    *resp.add_receipts() = std::move(receipt);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

chirp::chat::GetUnreadCountResponse ReadReceiptHandlers::HandleGetUnreadCount(
    const chirp::chat::GetUnreadCountRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::GetUnreadCountResponse resp;
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  int32_t total = 0;
  for (auto& unread : receipts_.GetAllUnread(req.user_id())) {
    total += unread.count();
    *resp.add_channels() = std::move(unread);
  }
  resp.set_total_unread(total);
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

// ---------------------------------------------------------------------------
// Typing indicators
// ---------------------------------------------------------------------------

TypingHandlers::TypingHandlers(TypingManager& typing, ChannelMemberResolver members,
                               UserNotifier notify)
    : typing_(typing), members_(std::move(members)), notify_(std::move(notify)) {}

bool TypingHandlers::HandleTypingIndicator(const chirp::chat::TypingIndicator& indicator,
                                           std::string_view authenticated_user_id) {
  if (indicator.channel_id().empty() ||
      !SameUser(authenticated_user_id, indicator.user_id())) {
    return false;
  }

  chirp::chat::TypingIndicator out;
  bool changed = false;
  if (indicator.is_typing()) {
    changed = typing_.UserStartedTyping(indicator.channel_id(), indicator.channel_type(),
                                        indicator.user_id(), indicator.username(), &out);
  } else {
    typing_.UserStoppedTyping(indicator.channel_id(), indicator.channel_type(),
                              indicator.user_id());
    // Stop events always propagate so clients can clear the row.
    out = indicator;
    out.set_timestamp(chirp::chat::runtime::NowMs());
    changed = true;
  }
  if (!changed) {
    return false;  // cooldown or unchanged state
  }

  for (const auto& member :
       members_(indicator.channel_type(), indicator.channel_id(), indicator.user_id())) {
    notify_(member, chirp::gateway::TYPING_INDICATOR_NOTIFY, out);
  }
  return true;
}

chirp::chat::GetTypingUsersResponse TypingHandlers::HandleGetTypingUsers(
    const chirp::chat::GetTypingUsersRequest& req, std::string_view /*authenticated_user_id*/) {
  chirp::chat::GetTypingUsersResponse resp;
  if (req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  for (const auto& user_id : typing_.GetTypingUsers(req.channel_id(), req.channel_type())) {
    resp.add_typing_user_ids(user_id);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

// ---------------------------------------------------------------------------
// Reactions
// ---------------------------------------------------------------------------

ReactionHandlers::ReactionHandlers(ReactionManager& reactions,
                                   ChannelMemberResolver members, UserNotifier notify)
    : reactions_(reactions), members_(std::move(members)), notify_(std::move(notify)) {}

void ReactionHandlers::TrackMessage(const std::string& message_id,
                                    chirp::chat::ChannelType channel_type,
                                    const std::string& channel_id) {
  if (message_id.empty() || channel_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  message_channels_[message_id] = {channel_type, channel_id};
}

bool ReactionHandlers::ChannelOfMessage(const std::string& message_id,
                                        chirp::chat::ChannelType* channel_type,
                                        std::string* channel_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_channels_.find(message_id);
  if (it == message_channels_.end()) {
    return false;
  }
  *channel_type = it->second.channel_type;
  *channel_id = it->second.channel_id;
  return true;
}

void ReactionHandlers::BroadcastReaction(chirp::gateway::MsgID msg_id,
                                         const std::string& message_id,
                                         const std::string& channel_id,
                                         chirp::chat::ChannelType channel_type,
                                         const std::string& emoji,
                                         const std::string& user_id) {
  chirp::chat::ChannelType resolved_type = channel_type;
  std::string resolved_channel = channel_id;
  if (resolved_channel.empty() &&
      !ChannelOfMessage(message_id, &resolved_type, &resolved_channel)) {
    return;  // message never seen on this server; nothing to broadcast to
  }

  chirp::chat::ReactionAddedNotify added;
  chirp::chat::ReactionRemovedNotify removed;
  google::protobuf::Message* body = nullptr;
  if (msg_id == chirp::gateway::REACTION_ADDED_NOTIFY) {
    added.set_message_id(message_id);
    added.set_channel_id(resolved_channel);
    added.set_emoji(emoji);
    added.set_user_id(user_id);
    added.set_timestamp(chirp::chat::runtime::NowMs());
    body = &added;
  } else {
    removed.set_message_id(message_id);
    removed.set_channel_id(resolved_channel);
    removed.set_emoji(emoji);
    removed.set_user_id(user_id);
    removed.set_timestamp(chirp::chat::runtime::NowMs());
    body = &removed;
  }

  for (const auto& member : members_(resolved_type, resolved_channel, user_id)) {
    notify_(member, msg_id, *body);
  }
}

chirp::chat::AddReactionResponse ReactionHandlers::HandleAddReaction(
    const chirp::chat::AddReactionRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::AddReactionResponse resp;

  if (req.message_id().empty() || req.emoji().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::MessageReaction reaction;
  if (!reactions_.AddReaction(req.message_id(), req.user_id(), req.emoji(), &reaction)) {
    resp.set_code(chirp::common::INVALID_PARAM);  // GCOVR_EXCL_LINE -- AddReaction has set semantics and never returns false
    return resp;  // GCOVR_EXCL_LINE -- AddReaction has set semantics and never returns false
  }

  resp.set_code(chirp::common::OK);
  *resp.mutable_reaction() = reaction;
  resp.set_server_time(chirp::chat::runtime::NowMs());

  BroadcastReaction(chirp::gateway::REACTION_ADDED_NOTIFY, req.message_id(), "",
                    chirp::chat::PRIVATE, req.emoji(), req.user_id());
  return resp;
}

chirp::chat::RemoveReactionResponse ReactionHandlers::HandleRemoveReaction(
    const chirp::chat::RemoveReactionRequest& req, std::string_view authenticated_user_id) {
  chirp::chat::RemoveReactionResponse resp;

  if (req.message_id().empty() || req.emoji().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  if (!reactions_.RemoveReaction(req.message_id(), req.user_id(), req.emoji())) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(chirp::chat::runtime::NowMs());

  BroadcastReaction(chirp::gateway::REACTION_REMOVED_NOTIFY, req.message_id(), "",
                    chirp::chat::PRIVATE, req.emoji(), req.user_id());
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

chirp::chat::GetReactionsResponse ReactionHandlers::HandleGetReactions(
    const chirp::chat::GetReactionsRequest& req, std::string_view /*authenticated_user_id*/) {
  chirp::chat::GetReactionsResponse resp;
  if (req.message_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!req.emoji().empty()) {
    chirp::chat::MessageReaction reaction;
    if (reactions_.GetReaction(req.message_id(), req.emoji(), &reaction)) {
      *resp.add_reactions() = std::move(reaction);
    }
  } else {
    for (auto& reaction : reactions_.GetReactions(req.message_id())) {
      *resp.add_reactions() = std::move(reaction);
    }
  }
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

// ---------------------------------------------------------------------------
// Message edit / delete
// ---------------------------------------------------------------------------

MessageEditHandlers::MessageEditHandlers(MessageEditManager& edits,
                                         ChannelMemberResolver members,
                                         ChannelModeratorChecker is_moderator,
                                         UserNotifier notify)
    : edits_(edits),
      members_(std::move(members)),
      is_moderator_(std::move(is_moderator)),
      notify_(std::move(notify)) {}

void MessageEditHandlers::TrackMessage(const std::string& message_id,
                                       chirp::chat::ChannelType channel_type,
                                       const std::string& channel_id) {
  if (message_id.empty() || channel_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  message_channels_[message_id] = {channel_type, channel_id};
}

bool MessageEditHandlers::ChannelOfMessage(const std::string& message_id,
                                           chirp::chat::ChannelType* channel_type,
                                           std::string* channel_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_channels_.find(message_id);
  if (it == message_channels_.end()) {
    return false;
  }
  *channel_type = it->second.first;
  *channel_id = it->second.second;
  return true;
}

chirp::chat::EditMessageResponse MessageEditHandlers::HandleEditMessage(
    const chirp::chat::EditMessageRequest& req,
    std::string_view authenticated_user_id) {
  chirp::chat::EditMessageResponse resp;

  if (req.message_id().empty() || req.new_content().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::ChannelType channel_type = chirp::chat::PRIVATE;
  std::string channel_id;
  if (!ChannelOfMessage(req.message_id(), &channel_type, &channel_id)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }

  chirp::chat::ChatMessageFull full;
  if (!edits_.EditMessage(req.message_id(), req.user_id(), req.new_content(), &full,
                          is_moderator_(channel_type, channel_id, req.user_id()))) {
    // The message exists here, so failure means permission, an expired edit
    // window, a deleted message, or the edit-count limit.
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::MessageEditedNotify notify;
  notify.set_message_id(req.message_id());
  notify.set_channel_id(channel_id);
  notify.set_new_content(req.new_content());
  notify.set_edited_at(full.edited_at());
  notify.set_edited_by(req.user_id());
  for (const auto& member : members_(channel_type, channel_id, req.user_id())) {
    notify_(member, chirp::gateway::MESSAGE_EDITED_NOTIFY, notify);
  }

  resp.set_code(chirp::common::OK);
  *resp.mutable_message() = std::move(full);
  resp.set_server_time(chirp::chat::runtime::NowMs());
  return resp;
}

chirp::chat::DeleteMessageResponse MessageEditHandlers::HandleDeleteMessage(
    const chirp::chat::DeleteMessageRequest& req,
    std::string_view authenticated_user_id) {
  chirp::chat::DeleteMessageResponse resp;

  if (req.message_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::ChannelType channel_type = chirp::chat::PRIVATE;
  std::string channel_id;
  if (!ChannelOfMessage(req.message_id(), &channel_type, &channel_id)) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    return resp;
  }

  const bool moderator = is_moderator_(channel_type, channel_id, req.user_id());
  if (!edits_.DeleteMessage(req.message_id(), req.user_id(), req.is_hard_delete(),
                            moderator)) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  chirp::chat::MessageDeletedNotify notify;
  notify.set_message_id(req.message_id());
  notify.set_channel_id(channel_id);
  notify.set_is_hard_delete(req.is_hard_delete());
  notify.set_deleted_by(req.user_id());
  notify.set_deleted_at(chirp::chat::runtime::NowMs());
  for (const auto& member : members_(channel_type, channel_id, req.user_id())) {
    notify_(member, chirp::gateway::MESSAGE_DELETED_NOTIFY, notify);
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(chirp::chat::runtime::NowMs());
  resp.set_was_permanently_deleted(req.is_hard_delete());
  return resp;
}

chirp::chat::BulkDeleteResponse MessageEditHandlers::HandleBulkDelete(
    const chirp::chat::BulkDeleteRequest& req,
    std::string_view authenticated_user_id) {
  chirp::chat::BulkDeleteResponse resp;

  if (req.message_ids().empty() || req.channel_id().empty()) {
    resp.set_code(chirp::common::INVALID_PARAM);
    return resp;
  }
  if (!SameUser(authenticated_user_id, req.requester_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }

  // Bulk delete targets group channels; the checker resolves moderator rights
  // from the group membership (false for anything else).
  const bool moderator =
      is_moderator_(chirp::chat::GUILD, req.channel_id(), req.requester_id());
  std::vector<std::string> failed =
      edits_.BulkDelete({req.message_ids().begin(), req.message_ids().end()},
                        req.channel_id(), req.requester_id(), moderator);

  chirp::chat::MessageDeletedNotify notify;
  notify.set_channel_id(req.channel_id());
  notify.set_is_hard_delete(false);
  notify.set_deleted_by(req.requester_id());
  notify.set_deleted_at(chirp::chat::runtime::NowMs());
  for (const auto& message_id : req.message_ids()) {
    const bool deleted =
        std::find(failed.begin(), failed.end(), message_id) == failed.end();
    if (!deleted) {
      continue;
    }
    notify.set_message_id(message_id);
    for (const auto& member :
         members_(chirp::chat::GUILD, req.channel_id(), req.requester_id())) {
      notify_(member, chirp::gateway::MESSAGE_DELETED_NOTIFY, notify);
    }
  }

  resp.set_code(chirp::common::OK);
  resp.set_deleted_count(static_cast<int32_t>(req.message_ids_size() -
                                              static_cast<int32_t>(failed.size())));
  for (auto&& message_id : failed) {
    *resp.add_failed_message_ids() = std::move(message_id);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());
  return resp;
}

// ---------------------------------------------------------------------------
// Mentions
// ---------------------------------------------------------------------------

MentionHandlers::MentionHandlers(MentionManager& mentions,
                                 ChannelModeratorChecker is_moderator)
    : mentions_(mentions), is_moderator_(std::move(is_moderator)) {}

bool MentionHandlers::ProcessOutgoingMessage(const chirp::chat::ChatMessage& msg,
                                             chirp::common::ErrorCode* reason) {
  if (reason != nullptr) {
    *reason = chirp::common::OK;
  }
  chirp::chat::ParsedMentions parsed =
      mentions_.ParseMentions(msg.content(), msg.sender_id());
  if (!parsed.mentions_everyone && !parsed.mentions_here) {
    return true;
  }

  const bool moderator = is_moderator_(msg.channel_type(), msg.channel_id(),
                                       msg.sender_id());
  if (!mentions_.CanMentionEveryone(msg.sender_id(), msg.channel_id(), moderator)) {
    if (reason != nullptr) {
      *reason = chirp::common::AUTH_FAILED;
    }
    return false;
  }
  mentions_.RecordEveryoneMention(msg.sender_id(), msg.channel_id());
  return true;
}

chirp::chat::GetMentionSuggestionsResponse
MentionHandlers::HandleGetMentionSuggestions(
    const chirp::chat::GetMentionSuggestionsRequest& req,
    std::string_view authenticated_user_id) {
  chirp::chat::GetMentionSuggestionsResponse resp;
  if (!SameUser(authenticated_user_id, req.user_id())) {
    resp.set_code(chirp::common::AUTH_FAILED);
    return resp;
  }
  for (const auto& suggestion :
       mentions_.GetMentionSuggestions(req.query(), req.channel_id(), req.user_id())) {
    chirp::chat::MentionSuggestion* entry = resp.add_suggestions();
    entry->set_display_text(suggestion.display_text);
    entry->set_id(suggestion.id);
    entry->set_type(suggestion.type);
    entry->set_icon_url(suggestion.icon_url);
  }
  resp.set_code(chirp::common::OK);
  return resp;
}  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered

}  // namespace chat
}  // namespace chirp
