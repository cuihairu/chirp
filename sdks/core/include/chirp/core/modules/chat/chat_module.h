#ifndef CHIRP_CORE_MODULES_CHAT_CHAT_MODULE_H_
#define CHIRP_CORE_MODULES_CHAT_CHAT_MODULE_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "chirp/core/modules/chat/chat_events.h"

namespace chirp {
namespace core {
namespace modules {
namespace chat {

// Message types
enum class MessageType {
  TEXT,
  EMOJI,
  VOICE,
  IMAGE,
  SYSTEM
};

// Channel types
enum class ChannelType {
  PRIVATE,    // 1v1 private chat
  TEAM,       // Team/group chat
  GUILD,      // Guild chat
  WORLD,      // World/global chat
  GROUP       // Custom group
};

// Chat message
struct Message {
  std::string message_id;
  std::string sender_id;
  std::string receiver_id;
  std::string channel_id;
  ChannelType channel_type;
  MessageType msg_type;
  std::string content;
  int64_t timestamp;
};

// Send result
enum class SendResult {
  SUCCESS,
  FAILED,
  RATE_LIMITED,
  OFFLINE
};

// Send message callback
using SendMessageCallback = std::function<void(SendResult result, const std::string& message_id)>;

// Get history callback
using GetHistoryCallback = std::function<void(bool success, const std::vector<Message>& messages, bool has_more)>;

// Chat module interface
class ChatModule {
public:
  virtual ~ChatModule() = default;

  // Send a message
  virtual void SendMessage(const std::string& to_user,
                          MessageType type,
                          const std::string& content,
                          SendMessageCallback callback) = 0;

  // Send message to a channel
  virtual void SendChannelMessage(const std::string& channel_id,
                                 ChannelType channel_type,
                                 MessageType type,
                                 const std::string& content,
                                 SendMessageCallback callback) = 0;

  // Get message history
  virtual void GetHistory(const std::string& channel_id,
                         ChannelType channel_type,
                         int64_t before_timestamp,
                         int32_t limit,
                         GetHistoryCallback callback) = 0;

  // Mark message as read
  virtual void MarkRead(const std::string& channel_id,
                       ChannelType channel_type,
                       const std::string& message_id) = 0;

  // Get unread count
  virtual void GetUnreadCount(std::function<void(int32_t count)> callback) = 0;

  // Event callbacks
  virtual void SetMessageCallback(MessageCallback callback) = 0;
  virtual void SetMessageReadCallback(MessageReadCallback callback) = 0;
  virtual void SetTypingCallback(TypingCallback callback) = 0;

  // Group operations
  virtual void CreateGroup(const std::string& name,
                          const std::vector<std::string>& members,
                          std::function<void(const std::string& group_id)> callback) = 0;
  virtual void JoinGroup(const std::string& group_id,
                        std::function<void(bool success)> callback) = 0;
  virtual void LeaveGroup(const std::string& group_id,
                         std::function<void(bool success)> callback) = 0;
  virtual void GetGroupMembers(const std::string& group_id,
                              std::function<void(const std::vector<GroupMember>& members)> callback) = 0;

  // Typing indicator
  virtual void SendTypingIndicator(const std::string& channel_id,
                                  ChannelType channel_type,
                                  bool is_typing) = 0;
};

} // namespace chat
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_CHAT_CHAT_MODULE_H_
