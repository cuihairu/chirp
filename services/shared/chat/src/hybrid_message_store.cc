#include "hybrid_message_store.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>

#include "logger.h"
#include "message_store_factory.h"
#include "proto/chat.pb.h"

namespace chirp::chat {
namespace {

using Logger = chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Non-throwing integer parse for values read back from shared Redis.
// std::stoll aborts the process via an unhandled invalid_argument when the
// string has no leading digits, and one stale/malformed entry must not take
// the delivery tracker (and with it the whole io thread) down.
bool ParseI64(const std::string& text, int64_t* out) {
  if (text.empty()) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (end == text.c_str() || errno == ERANGE) {
    return false;
  }
  *out = static_cast<int64_t>(value);
  return true;
}

} // namespace

std::string MessageData::SerializeAsString() const {
  ChatMessage msg;
  msg.set_message_id(message_id);
  msg.set_sender_id(sender_id);
  msg.set_receiver_id(receiver_id);
  msg.set_channel_id(channel_id);
  msg.set_channel_type(static_cast<ChannelType>(channel_type));
  msg.set_msg_type(static_cast<MsgType>(msg_type));
  msg.set_content(content);
  msg.set_timestamp(timestamp);
  msg.set_reply_to_message_id(reply_to_message_id);
  return msg.SerializeAsString();
}

bool MessageData::ParseFromArray(const void* data, int size) {
  ChatMessage msg;
  if (!msg.ParseFromArray(data, size)) {
    return false;
  }

  message_id = msg.message_id();
  sender_id = msg.sender_id();
  receiver_id = msg.receiver_id();
  channel_id = msg.channel_id();
  channel_type = static_cast<int>(msg.channel_type());
  msg_type = static_cast<int>(msg.msg_type());
  content = msg.content();
  timestamp = msg.timestamp();
  reply_to_message_id = msg.reply_to_message_id();
  created_at = msg.timestamp();
  return true;
}

HybridMessageStore::HybridMessageStore(asio::io_context& io,
                                      const MessageStoreConfig& config)
    : io_(io), config_(config) {

  // Create Redis client
  redis_ = std::make_shared<network::RedisClient>(config_.redis_host, config_.redis_port);

  // Create MySQL connection pool and archive store (backend chosen by the
  // factory; hybrid code only sees the MessageStore interface)
  mysql_pool_ = MakeConnectionPool(config_);
  mysql_store_ = MakeMessageStore(mysql_pool_);
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

  // 2. Store in MySQL for persistence
  StoredMessage mysql_msg;
  mysql_msg.message_id = message.message_id;
  mysql_msg.sender_id = message.sender_id;
  mysql_msg.receiver_id = message.receiver_id;
  mysql_msg.channel_id = message.channel_id;
  mysql_msg.channel_type = message.channel_type;
  mysql_msg.msg_type = message.msg_type;
  mysql_msg.content = message.content;
  mysql_msg.timestamp = message.timestamp;
  mysql_msg.created_at = message.created_at;
  mysql_msg.reply_to_message_id = message.reply_to_message_id;

  bool mysql_result = mysql_store_->StoreMessage(mysql_msg);

  // Offline queueing is owned by the caller (the send handler decides via
  // the router's delivery count whether the receiver actually needs an
  // offline copy), so StoreMessage only persists the history tiers.

  return mysql_result;  // Return MySQL result as the source of truth
}

void HybridMessageStore::StoreMessageAsync(const MessageData& message,
                                          std::function<void(bool)> callback) {
  // Store in Redis synchronously (fast path)
  std::string history_key = HistoryKey(message.channel_id);
  std::string msg_data = message.SerializeAsString();

  redis_->RPush(history_key, msg_data);

  // Post MySQL write to background thread
  asio::post(io_, [this, message, callback]() {
    StoredMessage mysql_msg;
    mysql_msg.message_id = message.message_id;
    mysql_msg.sender_id = message.sender_id;
    mysql_msg.receiver_id = message.receiver_id;
    mysql_msg.channel_id = message.channel_id;
    mysql_msg.channel_type = message.channel_type;
    mysql_msg.msg_type = message.msg_type;
    mysql_msg.content = message.content;
    mysql_msg.timestamp = message.timestamp;
    mysql_msg.created_at = message.created_at;
    mysql_msg.reply_to_message_id = message.reply_to_message_id;

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
        MessageData converted;
        converted.message_id = std::move(msg.message_id);
        converted.sender_id = std::move(msg.sender_id);
        converted.receiver_id = std::move(msg.receiver_id);
        converted.channel_id = std::move(msg.channel_id);
        converted.channel_type = msg.channel_type;
        converted.msg_type = msg.msg_type;
        converted.content = std::move(msg.content);
        converted.timestamp = msg.timestamp;
        converted.created_at = msg.created_at;
        converted.reply_to_message_id = msg.reply_to_message_id;
        results.push_back(std::move(converted));
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

bool HybridMessageStore::HasMessage(const std::string& channel_id,
                                    const std::string& message_id) {
  if (channel_id.empty() || message_id.empty()) {
    return false;
  }

  // Hot tier first: StoreMessage/StoreMessageAsync push to Redis
  // synchronously, so a just-sent message is always visible here even
  // before its async MySQL write lands.
  auto redis_messages = redis_->LRange(HistoryKey(channel_id), 0, -1);
  for (const auto& raw : redis_messages) {
    MessageData msg;
    if (msg.ParseFromArray(raw.data(), static_cast<int>(raw.size())) &&
        msg.message_id == message_id) {
      return true;
    }
  }

  // Cold tier: messages that already aged out of the Redis list.
  return mysql_store_->MessageExists(channel_id, message_id);
}

bool HybridMessageStore::AddOfflineMessage(const std::string& user_id,
                                           const std::string& serialized) {
  std::string offline_key = OfflineKey(user_id);
  if (redis_->RPush(offline_key, serialized) &&
      redis_->Expire(offline_key, config_.redis_offline_ttl_seconds)) {
    return true;
  }

  // Redis unavailable: keep the message in an in-memory fallback so
  // single-node deployments (no Redis) still refill on login.
  std::lock_guard<std::mutex> lock(offline_fallback_mutex_);
  auto& queue = offline_fallback_[user_id];
  queue.push_back(serialized);
  constexpr size_t kMaxFallbackPerUser = 1024;
  if (queue.size() > kMaxFallbackPerUser) {
    queue.pop_front();
  }
  return false;
}

bool HybridMessageStore::RemoveOfflineMessage(const std::string& user_id,
                                              const std::string& serialized) {
  // Late-ack cleanup: remove the offline copy the client confirmed after it
  // had already been requeued. The Redis entry matches byte-for-byte; the
  // fallback queue holds serialized strings, so it matches directly too.
  std::string offline_key = OfflineKey(user_id);
  bool removed = redis_->LRem(offline_key, 1, serialized) > 0;

  std::lock_guard<std::mutex> lock(offline_fallback_mutex_);
  auto it = offline_fallback_.find(user_id);
  if (it != offline_fallback_.end()) {
    for (auto elem = it->second.begin(); elem != it->second.end(); ++elem) {
      if (*elem == serialized) {
        it->second.erase(elem);
        removed = true;
        break;
      }
    }
    if (it->second.empty()) {
      offline_fallback_.erase(it);
    }
  }
  return removed;
}

std::vector<MessageData> HybridMessageStore::GetOfflineMessages(const std::string& user_id) {  // GCOVR_EXCL_LINE -- unreachable exit-block line (gcc/NRVO artifact); body is covered
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

  // Merge in messages held by the Redis-down fallback.
  std::lock_guard<std::mutex> lock(offline_fallback_mutex_);
  auto it = offline_fallback_.find(user_id);
  if (it != offline_fallback_.end()) {
    for (const auto& msg_data : it->second) {
      MessageData msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        results.push_back(std::move(msg));
      }
    }
  }

  return results;
}

std::vector<MessageData> HybridMessageStore::PopOfflineMessages(const std::string& user_id) {
  auto messages = GetOfflineMessages(user_id);

  // Clear from Redis
  std::string offline_key = OfflineKey(user_id);
  redis_->Del(offline_key);

  // Drain the Redis-down fallback too.
  std::lock_guard<std::mutex> lock(offline_fallback_mutex_);
  offline_fallback_.erase(user_id);

  return messages;
}

bool HybridMessageStore::ClearOfflineMessages(const std::string& user_id) {
  std::string offline_key = OfflineKey(user_id);
  bool redis_cleared = redis_->Del(offline_key);

  std::lock_guard<std::mutex> lock(offline_fallback_mutex_);
  offline_fallback_.erase(user_id);

  return redis_cleared;
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

  // Parse status value: status:created_at or status:timestamp:error. Same
  // shared-Redis caveat as GetPendingDeliveries: a malformed value parses to
  // the default info instead of throwing on the io thread. created_at keeps
  // std::stoll's leading-digits semantics ("0:123:err" -> 123).
  std::string value = *result;
  size_t colon1 = value.find(':');
  if (colon1 != std::string::npos) {
    int64_t status = 0;
    int64_t created_at = 0;
    if (ParseI64(value.substr(0, colon1), &status) &&
        ParseI64(value.substr(colon1 + 1), &created_at)) {
      info.status = static_cast<DeliveryState>(status);
      info.created_at = created_at;

      size_t colon2 = value.find(':', colon1 + 1);
      if (colon2 != std::string::npos) {
        info.last_error = value.substr(colon2 + 1);
      }
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
    // Parse: message_id:receiver_id:expires_at — from both ends, because a
    // receiver_id may itself contain colons (NPC receivers are
    // "npc:<name>", so TrackMessage writes four-segment entries for them).
    // Message ids carry no colons and the expiry is pure digits, so the
    // first and last colon bound the receiver id exactly. Malformed or
    // stale entries are skipped rather than parsed: this list lives in
    // shared Redis and one bad entry must not abort the tracker sweep.
    const size_t colon1 = entry.find(':');
    const size_t colon2 = entry.rfind(':');
    if (colon1 == std::string::npos || colon2 == std::string::npos ||
        colon1 >= colon2) {
      continue;  // fewer than two colon-separated fields
    }
    int64_t expires_at = 0;
    if (!ParseI64(entry.substr(colon2 + 1), &expires_at)) {
      continue;  // non-numeric expiry (e.g. legacy colon-split garbage)
    }
    const std::string message_id = entry.substr(0, colon1);
    const std::string receiver_id = entry.substr(colon1 + 1, colon2 - colon1 - 1);

    if (expires_at < before_timestamp) {
      DeliveryInfo info;
      info.message_id = message_id;
      info.receiver_id = receiver_id;
      info.status = DeliveryState::kPending;
      info.created_at = NowMs();
      pending.push_back(std::move(info));
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
