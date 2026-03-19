#include "hybrid_message_store.h"

#include <chrono>
#include <mutex>
#include <sstream>

#include "logger.h"

namespace chirp::chat {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string ToHex(const uint8_t* data, size_t len) {
  static const char* kHexChars = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    out.push_back(kHexChars[(b >> 4) & 0x0F]);
    out.push_back(kHexChars[b & 0x0F]);
  }
  return out;
}

} // namespace

HybridMessageStore::HybridMessageStore(asio::io_context& io,
                                      const MessageStoreConfig& config)
    : io_(io), config_(config) {

  // Create Redis client
  redis_ = std::make_shared<network::RedisClient>(config_.redis_host, config_.redis_port);

  // Create MySQL connection pool
  mysql_pool_ = std::make_shared<MySQLConnectionPool>(
      config_.mysql_pool_size,
      config_.mysql_host,
      config_.mysql_port,
      config_.mysql_database,
      config_.mysql_user,
      config_.mysql_password);

  // Create MySQL message store
  mysql_store_ = std::make_shared<MySQLMessageStore>(mysql_pool_);
}

HybridMessageStore::~HybridMessageStore() {
  Shutdown();
}

bool HybridMessageStore::Initialize() {
  Logger::Instance().Info("Initializing HybridMessageStore...");

  // Initialize MySQL store
  if (!mysql_store_->Initialize()) {
    Logger::Instance().Error("Failed to initialize MySQLMessageStore");
    return false;
  }

  // Test Redis connection
  auto ping_result = redis_->Get("ping");
  if (!ping_result) {
    Logger::Instance().Warn("Redis connection failed, running in MySQL-only mode");
  }

  Logger::Instance().Info("HybridMessageStore initialized");
  return true;
}

void HybridMessageStore::Shutdown() {
  Logger::Instance().Info("Shutting down HybridMessageStore...");
}

bool HybridMessageStore::StoreMessage(const MessageData& message) {
  // 1. Store in Redis for fast access
  std::string history_key = HistoryKey(message.channel_id);
  std::string msg_data = message.SerializeAsString();

  redis_->RPush(history_key, msg_data);
  redis_->LTrim(history_key, -config_.redis_history_limit, -1);

  // 2. Store in MySQL for persistence
  MySQLMessageStore::MessageData mysql_msg;
  mysql_msg.message_id = message.message_id;
  mysql_msg.sender_id = message.sender_id;
  mysql_msg.receiver_id = message.receiver_id;
  mysql_msg.channel_id = message.channel_id;
  mysql_msg.channel_type = message.channel_type;
  mysql_msg.msg_type = message.msg_type;
  mysql_msg.content = message.content;
  mysql_msg.timestamp = message.timestamp;
  mysql_msg.created_at = message.created_at;

  bool mysql_result = mysql_store_->StoreMessage(mysql_msg);

  // 3. Add to offline queue if there's a specific receiver
  if (!message.receiver_id.empty() && message.channel_type == 0) {  // PRIVATE
    std::string offline_key = OfflineKey(message.receiver_id);
    redis_->RPush(offline_key, msg_data);
    redis_->Expire(offline_key, config_.redis_offline_ttl_seconds);
  }

  return mysql_result;  // Return MySQL result as the source of truth
}

void HybridMessageStore::StoreMessageAsync(const MessageData& message,
                                          std::function<void(bool)> callback) {
  // Store in Redis synchronously (fast path)
  std::string history_key = HistoryKey(message.channel_id);
  std::string msg_data = message.SerializeAsString();

  redis_->RPush(history_key, msg_data);
  redis_->LTrim(history_key, -config_.redis_history_limit, -1);

  // For offline messages
  if (!message.receiver_id.empty() && message.channel_type == 0) {
    std::string offline_key = OfflineKey(message.receiver_id);
    redis_->RPush(offline_key, msg_data);
    redis_->Expire(offline_key, config_.redis_offline_ttl_seconds);
  }

  // Post MySQL write to background thread
  asio::post(io_, [this, message, callback]() {
    MySQLMessageStore::MessageData mysql_msg;
    mysql_msg.message_id = message.message_id;
    mysql_msg.sender_id = message.sender_id;
    mysql_msg.receiver_id = message.receiver_id;
    mysql_msg.channel_id = message.channel_id;
    mysql_msg.channel_type = message.channel_type;
    mysql_msg.msg_type = message.msg_type;
    mysql_msg.content = message.content;
    mysql_msg.timestamp = message.timestamp;
    mysql_msg.created_at = message.created_at;

    bool result = mysql_store_->StoreMessage(mysql_msg);

    if (callback) {
      callback(result);
    }
  });
}

std::vector<MessageData> HybridMessageStore::GetHistory(const std::string& channel_id,
                                                       int channel_type,
                                                       int64_t before_timestamp,
                                                       int32_t limit) {
  // First try Redis (hot data)
  std::string history_key = HistoryKey(channel_id);
  auto redis_messages = redis_->LRange(history_key, -limit, -1);

  std::vector<MessageData> results;

  if (!redis_messages.empty()) {
    // Parse Redis messages
    for (const auto& msg_data : redis_messages) {
      MessageData msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        if (before_timestamp <= 0 || msg.timestamp < before_timestamp) {
          results.push_back(std::move(msg));
        }
      }
    }
  }

  // If Redis doesn't have enough, fall back to MySQL
  if (static_cast<int>(results.size()) < limit) {
    int needed = limit - static_cast<int>(results.size());
    auto mysql_messages = mysql_store_->GetHistory(channel_id, channel_type,
                                                   before_timestamp, needed);

    // Merge results (avoiding duplicates)
    for (auto& msg : mysql_messages) {
      bool duplicate = false;
      for (const auto& existing : results) {
        if (existing.message_id == msg.message_id) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        results.push_back(std::move(msg));
      }
    }
  }

  // Sort by timestamp
  std::sort(results.begin(), results.end(),
           [](const MessageData& a, const MessageData& b) {
             return a.timestamp < b.timestamp;
           });

  return results;
}

std::vector<MessageData> HybridMessageStore::GetHistoryV2(const std::string& channel_id,
                                                         int channel_type,
                                                         const std::string& cursor,
                                                         int32_t limit,
                                                         std::string* next_cursor) {
  // For cursor-based pagination, we use timestamp-based cursors
  int64_t before_timestamp = 0;

  if (!cursor.empty()) {
    // Decode cursor (format: "timestamp:index")
    size_t colon_pos = cursor.find(':');
    if (colon_pos != std::string::npos) {
      before_timestamp = std::stoll(cursor.substr(0, colon_pos));
    }
  }

  auto messages = GetHistory(channel_id, channel_type, before_timestamp, limit);

  // Generate next cursor if there are more messages
  if (next_cursor && !messages.empty()) {
    if (static_cast<int>(messages.size()) >= limit) {
      int64_t oldest_timestamp = messages.front().timestamp;
      *next_cursor = std::to_string(oldest_timestamp) + ":0";
    } else {
      *next_cursor = "";  // No more pages
    }
  }

  return messages;
}

std::vector<MessageData> HybridMessageStore::GetOfflineMessages(const std::string& user_id) {
  std::string offline_key = OfflineKey(user_id);
  auto redis_messages = redis_->LRange(offline_key, 0, -1);

  std::vector<MessageData> results;
  results.reserve(redis_messages.size());

  for (const auto& msg_data : redis_messages) {
    MessageData msg;
    if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
      results.push_back(std::move(msg));
    }
  }

  return results;
}

std::vector<MessageData> HybridMessageStore::PopOfflineMessages(const std::string& user_id) {
  auto messages = GetOfflineMessages(user_id);

  // Clear from Redis
  std::string offline_key = OfflineKey(user_id);
  redis_->Del(offline_key);

  return messages;
}

bool HybridMessageStore::ClearOfflineMessages(const std::string& user_id) {
  std::string offline_key = OfflineKey(user_id);
  return redis_->Del(offline_key);
}

std::string HybridMessageStore::TrackMessage(const std::string& message_id,
                                            const std::string& receiver_id,
                                            int64_t expires_at) {
  std::string tracking_id = "track_" + message_id + "_" + std::to_string(NowMs());

  std::string delivery_key = DeliveryKey(message_id, receiver_id);
  std::string status_value = "0:" + std::to_string(NowMs());  // status:created_at

  redis_->SetEx(delivery_key, status_value,
               static_cast<int>((expires_at - NowMs()) / 1000));

  // Add to pending queue
  std::string pending_key = PendingDeliveryKey();
  std::string pending_value = message_id + ":" + receiver_id + ":" + std::to_string(expires_at);
  redis_->RPush(pending_key, pending_value);

  return tracking_id;
}

bool HybridMessageStore::AcknowledgeMessage(const std::string& message_id,
                                           const std::string& user_id) {
  std::string delivery_key = DeliveryKey(message_id, user_id);

  // Update status to delivered
  std::string status_value = "1:" + std::to_string(NowMs());  // DELIVERED:timestamp
  redis_->SetEx(delivery_key, status_value, 3600);  // Keep for 1 hour

  return true;
}

bool HybridMessageStore::FailMessage(const std::string& message_id,
                                    const std::string& user_id,
                                    const std::string& error) {
  std::string delivery_key = DeliveryKey(message_id, user_id);

  // Update status to failed
  std::string status_value = "2:" + std::to_string(NowMs()) + ":" + error;  // FAILED:timestamp:error
  redis_->SetEx(delivery_key, status_value, 86400);  // Keep for 24 hours

  return true;
}

std::optional<DeliveryInfo> HybridMessageStore::GetDeliveryStatus(const std::string& message_id,
                                                                const std::string& receiver_id) {
  std::string delivery_key = DeliveryKey(message_id, receiver_id);
  auto result = redis_->Get(delivery_key);

  if (!result) {
    return std::nullopt;
  }

  DeliveryInfo info;
  info.message_id = message_id;
  info.receiver_id = receiver_id;

  // Parse status value: status:created_at or status:timestamp:error
  std::string value = *result;
  size_t colon1 = value.find(':');
  if (colon1 != std::string::npos) {
    int status = std::stoi(value.substr(0, colon1));
    info.status = static_cast<DeliveryStatus>(status);
    info.created_at = std::stoll(value.substr(colon1 + 1));

    size_t colon2 = value.find(':', colon1 + 1);
    if (colon2 != std::string::npos) {
      info.last_error = value.substr(colon2 + 1);
    }
  }

  return info;
}

std::vector<DeliveryInfo> HybridMessageStore::GetPendingDeliveries(int64_t before_timestamp) {
  std::vector<DeliveryInfo> pending;

  // Get all pending delivery entries
  std::string pending_key = PendingDeliveryKey();
  auto entries = redis_->LRange(pending_key, 0, -1);

  for (const auto& entry : entries) {
    // Parse: message_id:receiver_id:expires_at
    size_t colon1 = entry.find(':');
    size_t colon2 = entry.find(':', colon1 + 1);

    if (colon1 != std::string::npos && colon2 != std::string::npos) {
      std::string message_id = entry.substr(0, colon1);
      std::string receiver_id = entry.substr(colon1 + 1, colon2 - colon1 - 1);
      int64_t expires_at = std::stoll(entry.substr(colon2 + 1));

      if (expires_at < before_timestamp) {
        DeliveryInfo info;
        info.message_id = message_id;
        info.receiver_id = receiver_id;
        info.status = DeliveryStatus::kPending;
        info.created_at = NowMs();
        pending.push_back(std::move(info));
      }
    }
  }

  return pending;
}

std::string HybridMessageStore::PrivateChannelId(const std::string& a, const std::string& b) {
  return a < b ? a + "|" + b : b + "|" + a;
}

std::string HybridMessageStore::OfflineKey(const std::string& user_id) {
  return "chirp:chat:offline:" + user_id;
}

std::string HybridMessageStore::HistoryKey(const std::string& channel_id) {
  return "chirp:chat:history:" + channel_id;
}

std::string HybridMessageStore::DeliveryKey(const std::string& message_id,
                                           const std::string& receiver_id) {
  return "chirp:chat:delivery:" + message_id + ":" + receiver_id;
}

std::string HybridMessageStore::PendingDeliveryKey() {
  return "chirp:chat:pending_delivery";
}

} // namespace chirp::chat
