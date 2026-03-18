#include "message_edit_manager.h"

#include <algorithm>

namespace chirp {
namespace chat {

MessageEditManager::MessageEditManager(const EditConfig& config)
    : config_(config) {}

void MessageEditManager::RegisterMessage(const std::string& message_id,
                                        const std::string& sender_id,
                                        const std::string& content) {
  std::lock_guard<std::mutex> lock(mu_);

  auto data = std::make_shared<MessageEditData>();
  data->message_id = message_id;
  data->sender_id = sender_id;
  data->current_content = content;
  data->created_at = GetCurrentTimeMs();
  data->edited_at = 0;
  data->edit_count = 0;
  data->is_deleted = false;

  messages_[message_id] = data;
  sender_to_messages_[sender_id].insert(message_id);
}

bool MessageEditManager::EditMessage(const std::string& message_id,
                                    const std::string& user_id,
                                    const std::string& new_content,
                                    ChatMessageFull* out_message) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return false;  // Message not found
  }

  auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  // Check permissions
  if (data->sender_id != user_id && !config_.allow_mod_edit) {
    return false;  // Not authorized
  }

  // Check if deleted
  if (data->is_deleted) {
    return false;  // Cannot edit deleted messages
  }

  // Check time window
  if (data->sender_id == user_id && IsEditWindowExpired(*data)) {
    return false;  // Edit window expired
  }

  // Check edit limit
  if (HasReachedEditLimit(*data)) {
    return false;  // Too many edits
  }

  // Create edit history entry
  MessageEdit edit;
  edit.set_old_content(data->current_content);
  edit.set_new_content(new_content);
  edit.set_edited_at(GetCurrentTimeMs());
  edit.set_edited_by(user_id);

  // Update message data
  data->current_content = new_content;
  data->edited_at = edit.edited_at();
  data->edit_count++;

  // Add to history (limit size)
  size_t history_size = data->edit_history.size();
  size_t new_entry_size = edit.old_content().size() + edit.new_content().size();

  if (history_size > 0 || new_entry_size < config_.max_edit_history_size) {
    data->edit_history.push_back(edit);
  }

  // Build output if requested
  if (out_message) {
    out_message->set_message_id(data->message_id);
    out_message->set_sender_id(data->sender_id);
    out_message->set_content(data->current_content);
    out_message->set_timestamp(data->created_at);
    out_message->set_is_edited(true);
    out_message->set_edited_at(data->edited_at);
    out_message->set_edit_count(data->edit_count);

    for (const auto& e : data->edit_history) {
      auto* entry = out_message->add_edit_history();
      *entry = e;
    }
  }

  return true;
}

bool MessageEditManager::DeleteMessage(const std::string& message_id,
                                      const std::string& user_id,
                                      bool is_hard_delete,
                                      bool is_moderator) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return false;
  }

  auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  // Check permissions
  if (data->sender_id != user_id && !is_moderator) {
    return false;
  }

  // Only moderators can hard delete
  if (is_hard_delete && !is_moderator) {
    return false;
  }

  if (is_hard_delete) {
    // Permanently delete
    messages_.erase(it);
    sender_to_messages_[data->sender_id].erase(message_id);
    return true;
  }

  // Soft delete
  data->is_deleted = true;
  data->deleted_at = GetCurrentTimeMs();
  data->deleted_by = user_id;

  // Move to deleted messages for retention
  deleted_messages_[message_id] = data;

  return true;
}

std::vector<std::string> MessageEditManager::BulkDelete(
    const std::vector<std::string>& message_ids,
    const std::string& channel_id,
    const std::string& requester_id,
    bool is_moderator) {

  std::vector<std::string> failed;

  for (const auto& message_id : message_ids) {
    if (!DeleteMessage(message_id, requester_id, false, is_moderator)) {
      failed.push_back(message_id);
    }
  }

  return failed;
}

bool MessageEditManager::GetFullMessage(const std::string& message_id,
                                       ChatMessageFull* out_message) {
  if (!out_message) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return false;
  }

  const auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  out_message->set_message_id(data->message_id);
  out_message->set_sender_id(data->sender_id);
  out_message->set_content(data->current_content);
  out_message->set_timestamp(data->created_at);
  out_message->set_is_deleted(data->is_deleted);
  out_message->set_deleted_at(data->deleted_at);
  out_message->set_deleted_by(data->deleted_by);
  out_message->set_is_edited(data->edit_count > 0);
  out_message->set_edited_at(data->edited_at);
  out_message->set_edit_count(data->edit_count);

  for (const auto& edit : data->edit_history) {
    auto* e = out_message->add_edit_history();
    *e = edit;
  }

  return true;
}

bool MessageEditManager::CanEdit(const std::string& message_id,
                                const std::string& user_id,
                                bool is_moderator) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return false;
  }

  const auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  // Mods can edit if allowed
  if (is_moderator && config_.allow_mod_edit) {
    return !data->is_deleted;
  }

  // User can edit their own messages
  if (data->sender_id != user_id) {
    return false;
  }

  return !data->is_deleted && !IsEditWindowExpired(*data) &&
         !HasReachedEditLimit(*data);
}

bool MessageEditManager::CanDelete(const std::string& message_id,
                                  const std::string& user_id,
                                  bool is_moderator) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return false;
  }

  const auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  // Mods can delete any message
  if (is_moderator) {
    return true;
  }

  // Users can delete their own messages
  return data->sender_id == user_id;
}

std::vector<MessageEdit> MessageEditManager::GetEditHistory(
    const std::string& message_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = messages_.find(message_id);
  if (it == messages_.end()) {
    return {};
  }

  const auto& data = it->second;
  std::lock_guard<std::mutex> data_lock(data->mu);

  return data->edit_history;
}

void MessageEditManager::CleanupOldDeletedMessages() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t cutoff_time = GetCurrentTimeMs() -
      (config_.soft_delete_retention_days * 24 * 60 * 60 * 1000);

  for (auto it = deleted_messages_.begin(); it != deleted_messages_.end();) {
    const auto& data = it->second;
    std::lock_guard<std::mutex> data_lock(data->mu);

    if (data->deleted_at < cutoff_time) {
      it = deleted_messages_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t MessageEditManager::GetTrackedMessageCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return messages_.size();
}

size_t MessageEditManager::GetDeletedMessageCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return deleted_messages_.size();
}

bool MessageEditManager::IsEditWindowExpired(const MessageEditData& data) const {
  if (config_.edit_time_window_ms <= 0) {
    return false;  // No time limit
  }

  int64_t elapsed = GetCurrentTimeMs() - data.created_at;
  return elapsed > config_.edit_time_window_ms;
}

bool MessageEditManager::HasReachedEditLimit(const MessageEditData& data) const {
  if (config_.max_edit_count <= 0) {
    return false;  // No limit
  }
  return data.edit_count >= config_.max_edit_count;
}

int64_t MessageEditManager::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace chat
} // namespace chirp
