#include "read_receipt_manager.h"

namespace chirp {
namespace chat {

void ReadReceiptManager::MarkRead(const std::string& user_id,
                                 const std::string& channel_id,
                                 chirp::chat::ChannelType channel_type,
                                 const std::string& message_id,
                                 int64_t read_timestamp) {
  std::string channel_key = ChannelKey(channel_type, channel_id);

  std::lock_guard<std::mutex> lock(mu_);

  // Update user's read cursor
  auto& user_cursors = user_cursors_[user_id];
  auto& cursor = user_cursors[channel_key];
  cursor.channel_key = channel_key;
  cursor.last_read_message_id = message_id;
  cursor.last_read_timestamp = read_timestamp;

  // Store receipt for the message
  ReadReceiptData receipt;
  receipt.user_id = user_id;
  receipt.message_id = message_id;
  receipt.read_at = read_timestamp;
  message_receipts_[message_id].push_back(receipt);

  // Track message if not already tracked
  if (message_to_channel_.find(message_id) == message_to_channel_.end()) {
    message_to_channel_[message_id] = channel_key;
  }
}

std::vector<chirp::chat::ReadReceipt> ReadReceiptManager::GetReadReceipts(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(mu_);

  std::vector<chirp::chat::ReadReceipt> result;
  auto it = message_receipts_.find(message_id);
  if (it != message_receipts_.end()) {
    result.reserve(it->second.size());
    for (const auto& data : it->second) {
      chirp::chat::ReadReceipt receipt;
      receipt.set_user_id(data.user_id);
      receipt.set_message_id(data.message_id);
      receipt.set_read_at(data.read_at);
      result.push_back(std::move(receipt));
    }
  }
  return result;
}

int32_t ReadReceiptManager::GetUnreadCount(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  int32_t total = 0;
  auto user_it = user_cursors_.find(user_id);
  if (user_it != user_cursors_.end()) {
    for (const auto& kv : user_it->second) {
      total += kv.second.unread_count;
    }
  }
  return total;
}

std::vector<chirp::chat::GetUnreadCountResponse_ChannelUnread>
ReadReceiptManager::GetAllUnread(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  std::vector<chirp::chat::GetUnreadCountResponse_ChannelUnread> result;

  auto user_it = user_cursors_.find(user_id);
  if (user_it != user_cursors_.end()) {
    result.reserve(user_it->second.size());
    for (const auto& kv : user_it->second) {
      chirp::chat::GetUnreadCountResponse_ChannelUnread channel;
      channel.set_channel_id(kv.second.last_read_message_id);
      // Parse channel type and id from key
      size_t colon_pos = kv.first.find(':');
      if (colon_pos != std::string::npos) {
        int type_val = std::stoi(kv.first.substr(0, colon_pos));
        channel.set_channel_type(static_cast<chirp::chat::ChannelType>(type_val));
        channel.set_channel_id(kv.first.substr(colon_pos + 1));
      }
      channel.set_count(kv.second.unread_count);
      channel.set_last_message_id(kv.second.last_read_message_id);
      result.push_back(std::move(channel));
    }
  }

  return result;
}

bool ReadReceiptManager::GetReadCursor(const std::string& user_id,
                                      const std::string& channel_id,
                                      chirp::chat::ChannelType channel_type,
                                      std::string* last_read_id,
                                      int64_t* last_read_timestamp) {
  std::string channel_key = ChannelKey(channel_type, channel_id);

  std::lock_guard<std::mutex> lock(mu_);

  auto user_it = user_cursors_.find(user_id);
  if (user_it == user_cursors_.end()) {
    return false;
  }

  auto cursor_it = user_it->second.find(channel_key);
  if (cursor_it == user_it->second.end()) {
    return false;
  }

  if (last_read_id) {
    *last_read_id = cursor_it->second.last_read_message_id;
  }
  if (last_read_timestamp) {
    *last_read_timestamp = cursor_it->second.last_read_timestamp;
  }
  return true;
}

void ReadReceiptManager::TrackMessage(const std::string& message_id,
                                     const std::string& channel_id,
                                     chirp::chat::ChannelType channel_type) {
  std::string channel_key = ChannelKey(channel_type, channel_id);

  std::lock_guard<std::mutex> lock(mu_);
  if (message_to_channel_.find(message_id) == message_to_channel_.end()) {
    message_to_channel_[message_id] = channel_key;
  }
}

} // namespace chat
} // namespace chirp
