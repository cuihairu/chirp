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
  // 撤回窗口（game_chat_features P0「消息撤回」）：发送者只能在这个时间窗内
  // 撤回自己发的消息，0 = 不限。版主删除（DeleteMessage 治理路径）不受此约束。
  int64_t recall_time_window_ms = 2 * 60 * 1000;  // 2 minutes default
  // 可撤回的频道（默认私聊 + 公会，即 P0 口径）。空列表 = 全部频道都不可撤回
  // （fail-closed），运营要放开某频道时显式加进来。
  std::vector<chirp::chat::ChannelType> recall_channel_types = {chirp::chat::PRIVATE,
                                                                 chirp::chat::GUILD};
};

// 撤回结果（DELETE_MESSAGE 非版主路径）。版主删除走 DeleteMessage 的 bool
// 契约，撤回需要把「为什么不行」告诉接入方，所以单列一个状态枚举。
enum class RecallStatus {
  kRecalled = 0,             // 已撤回（软删 + 广播 MESSAGE_DELETED_NOTIFY）
  kNotFound,                 // 消息不在本进程的编辑台账里
  kNotSender,                // 不是发送者本人（版主请走治理路径）
  kNotRecallableChannel,     // 该频道不在 recall_channel_types 内
  kWindowExpired,            // 超过撤回窗口
  kAlreadyRecalled,          // 已经撤回过，不重复广播
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

  // Edit a message (sender or, when allowed, a moderator)
  bool EditMessage(const std::string& message_id,
                   const std::string& user_id,
                   const std::string& new_content,
                   ChatMessageFull* out_message = nullptr,
                   bool is_moderator = false);

  // Delete a message (soft delete)
  bool DeleteMessage(const std::string& message_id,
                    const std::string& user_id,
                    bool is_hard_delete,
                    bool is_moderator = false);

  // 撤回（game_chat_features P0「消息撤回」）：发送者在撤回窗口内撤回自己
  // 发的消息。版主删除不受窗口/频道约束，走 DeleteMessage。
  // 频道类型由 handler 从 message_id 反查后传入。
  RecallStatus RecallMessage(const std::string& message_id,
                             const std::string& user_id,
                             chirp::chat::ChannelType channel_type);

  // 撤回预检（客户端决定是否显示「撤回」按钮的同一套规则）。
  bool CanRecall(const std::string& message_id,
                 const std::string& user_id,
                 chirp::chat::ChannelType channel_type);

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
  bool IsRecallWindowExpired(const MessageEditData& data) const;
  bool IsRecallableChannel(chirp::chat::ChannelType channel_type) const;
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
