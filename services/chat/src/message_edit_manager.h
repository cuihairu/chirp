#ifndef CHIRP_SERVICES_CHAT_MESSAGE_EDIT_MANAGER_H_
#define CHIRP_SERVICES_CHAT_MESSAGE_EDIT_MANAGER_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Configuration for message editing
struct EditConfig {
  int64_t edit_time_window_ms = 15 * 60 * 1000;  // 15 minutes default
  int32_t max_edit_count = 100;                   // Max edits per message
  int64_t max_edit_history_size = 10240;          // 10KB of edit history
  bool allow_mod_edit = true;                     // Mods can edit any message
  int32_t soft_delete_retention_days = 30;        // Keep deleted messages
};

// Message edit data
struct MessageEditData {
  std::string message_id;
  std::string sender_id;
  std::string current_content;
  int64_t created_at = 0;
  int64_t edited_at = 0;
  int32_t edit_count = 0;
  bool is_deleted = false;
  int64_t deleted_at = 0;
  std::string deleted_by;

  std::vector<MessageEdit> edit_history;

  mutable std::mutex mu;
};

// Manages message editing and deletion
class MessageEditManager {
public:
  explicit MessageEditManager(const EditConfig& config = EditConfig());
  ~MessageEditManager() = default;

  // Register a message for edit tracking
  void RegisterMessage(const std::string& message_id,
                     const std::string& sender_id,
                     const std::string& content);

  // Edit a message
  bool EditMessage(const std::string& message_id,
                  const std::string& user_id,
                  const std::string& new_content,
                  ChatMessageFull* out_message = nullptr);

  // Delete a message (soft delete)
  bool DeleteMessage(const std::string& message_id,
                    const std::string& user_id,
                    bool is_hard_delete,
                    bool is_moderator = false);

  // Bulk delete messages
  std::vector<std::string> BulkDelete(
      const std::vector<std::string>& message_ids,
      const std::string& channel_id,
      const std::string& requester_id,
      bool is_moderator);

  // Get full message data
  bool GetFullMessage(const std::string& message_id,
                     ChatMessageFull* out_message);

  // Check if message can be edited
  bool CanEdit(const std::string& message_id,
              const std::string& user_id,
              bool is_moderator = false);

  // Check if message can be deleted
  bool CanDelete(const std::string& message_id,
                const std::string& user_id,
                bool is_moderator = false);

  // Get edit history
  std::vector<MessageEdit> GetEditHistory(const std::string& message_id);

  // Clear old deleted messages
  void CleanupOldDeletedMessages();

  // Get statistics
  size_t GetTrackedMessageCount() const;
  size_t GetDeletedMessageCount() const;

private:
  bool IsEditWindowExpired(const MessageEditData& data) const;
  bool HasReachedEditLimit(const MessageEditData& data) const;
  int64_t GetCurrentTimeMs() const;

  EditConfig config_;
  mutable std::mutex mu_;

  // Message storage
  std::unordered_map<std::string, std::shared_ptr<MessageEditData>> messages_;

  // Index by sender for quick lookup
  std::unordered_map<std::string, std::unordered_set<std::string>> sender_to_messages_;

  // Deleted messages for retention
  std::unordered_map<std::string, std::shared_ptr<MessageEditData>> deleted_messages_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_MESSAGE_EDIT_MANAGER_H_
