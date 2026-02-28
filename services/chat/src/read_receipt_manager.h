#ifndef CHIRP_CHAT_READ_RECEIPT_MANAGER_H_
#define CHIRP_CHAT_READ_RECEIPT_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Read receipt data
struct ReadReceiptData {
  std::string user_id;
  std::string message_id;
  int64_t read_at;
};

// Per-user read cursor: channel_key -> last_read_message_id/timestamp
struct UserReadCursor {
  std::string channel_key;
  std::string last_read_message_id;
  int64_t last_read_timestamp;
  int32_t unread_count;
};

// Read receipt manager
class ReadReceiptManager {
public:
  ReadReceiptManager() = default;

  // Mark message as read
  void MarkRead(const std::string& user_id,
               const std::string& channel_id,
               chirp::chat::ChannelType channel_type,
               const std::string& message_id,
               int64_t read_timestamp);

  // Get read receipts for a message
  std::vector<chirp::chat::ReadReceipt> GetReadReceipts(const std::string& message_id);

  // Get unread count for a user
  int32_t GetUnreadCount(const std::string& user_id);

  // Get all unread counts per channel
  std::vector<chirp::chat::GetUnreadCountResponse_ChannelUnread> GetAllUnread(const std::string& user_id);

  // Get user's read cursor for a channel
  bool GetReadCursor(const std::string& user_id,
                    const std::string& channel_id,
                    chirp::chat::ChannelType channel_type,
                    std::string* last_read_id,
                    int64_t* last_read_timestamp);

  // Track message for read receipts (message_id -> channel_info)
  void TrackMessage(const std::string& message_id,
                   const std::string& channel_id,
                   chirp::chat::ChannelType channel_type);

private:
  std::string ChannelKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return std::to_string(static_cast<int>(type)) + ":" + channel_id;
  }

  std::mutex mu_;

  // message_id -> channel_key (for looking up receipts)
  std::unordered_map<std::string, std::string> message_to_channel_;

  // message_id -> list of read receipts
  std::unordered_map<std::string, std::vector<ReadReceiptData>> message_receipts_;

  // user_id -> channel_key -> read cursor
  std::unordered_map<std::string, std::unordered_map<std::string, UserReadCursor>> user_cursors_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_READ_RECEIPT_MANAGER_H_
