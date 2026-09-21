#ifndef CHIRP_CHAT_MESSAGE_STORE_H_
#define CHIRP_CHAT_MESSAGE_STORE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chirp {
namespace chat {

// Message data for archive storage (backend-neutral; the concrete backend —
// MySQL today, PostgreSQL later — is picked by message_store_factory.cc)
struct StoredMessage {
  std::string message_id;
  std::string sender_id;
  std::string receiver_id;
  std::string channel_id;
  int channel_type{0};
  int msg_type{0};
  std::string content;
  int64_t timestamp{0};
  int64_t created_at{0};
};

// Read receipt data
struct ReadReceiptData {
  std::string message_id;
  std::string user_id;
  int64_t read_at{0};
};

// Backend-neutral message archive interface. SQL dialects stay inside the
// implementations; consumers only ever see this header.
class MessageStore {
public:
  virtual ~MessageStore() = default;

  // Initialize database schema
  virtual bool Initialize() = 0;

  // Store message
  virtual bool StoreMessage(const StoredMessage& message) = 0;

  // Get message history
  virtual std::vector<StoredMessage> GetHistory(const std::string& channel_id,
                                                int channel_type,
                                                int64_t before_timestamp,
                                                int32_t limit) = 0;

  // Get offline messages for a user
  virtual std::vector<StoredMessage> GetOfflineMessages(const std::string& user_id) = 0;

  // Delete offline messages for a user
  virtual bool ClearOfflineMessages(const std::string& user_id) = 0;

  // Store read receipt
  virtual bool StoreReadReceipt(const std::string& message_id, const std::string& user_id,
                                int64_t read_at) = 0;

  // Get read receipts for a message
  virtual std::vector<ReadReceiptData> GetReadReceipts(const std::string& message_id) = 0;

  // Mark messages as read
  virtual bool MarkAsRead(const std::string& user_id, const std::string& channel_id,
                          int channel_type, const std::string& message_id, int64_t read_at) = 0;

  // Get unread count
  virtual int32_t GetUnreadCount(const std::string& user_id) = 0;

  // Get all unread counts per channel
  virtual std::vector<std::pair<std::string, int32_t>> GetAllUnread(const std::string& user_id) = 0;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_MESSAGE_STORE_H_
