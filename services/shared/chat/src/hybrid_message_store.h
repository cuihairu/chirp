#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <asio.hpp>

#include "message_store.h"
#include "message_store_config.h"
#include "network/redis_client.h"

namespace chirp::chat {

class MySQLConnectionPool;
class MySQLMessageStore;

/// @brief Message data structure for storage
struct MessageData {
  std::string message_id;
  std::string sender_id;
  std::string receiver_id;
  std::string channel_id;
  int channel_type{0};
  int msg_type{0};
  std::string content;
  int64_t timestamp{0};
  int64_t created_at{0};

  std::string SerializeAsString() const;
  bool ParseFromArray(const void* data, int size);
};

/// @brief Internal delivery state for a tracked message
enum class DeliveryState {
  kPending = 0,
  kDelivered = 1,
  kFailed = 2,
  kAcknowledged = 3
};

/// @brief Delivery tracking info
struct DeliveryInfo {
  std::string message_id;
  std::string receiver_id;
  DeliveryState status{DeliveryState::kPending};
  int64_t created_at{0};
  int64_t delivered_at{0};
  int retry_count{0};
  std::string last_error;
};

/// @brief Hybrid message store combining Redis (hot) and MySQL (cold)
/// Provides fast access to recent messages and persistent long-term storage
class HybridMessageStore {
public:
  using MessageCallback = std::function<void(const MessageData&)>;
  using DeliveryCallback = std::function<void(const std::string& message_id,
                                             const std::string& receiver_id,
                                             DeliveryState status)>;

  explicit HybridMessageStore(asio::io_context& io,
                              const MessageStoreConfig& config);
  ~HybridMessageStore();

  /// @brief Initialize the store
  bool Initialize();

  /// @brief Shutdown the store
  void Shutdown();

  /// @brief Store a message (writes to both Redis and MySQL)
  bool StoreMessage(const MessageData& message);

  /// @brief Store a message asynchronously
  void StoreMessageAsync(const MessageData& message,
                        std::function<void(bool)> callback = nullptr);

  /// @brief Get history with pagination
  std::vector<MessageData> GetHistory(const std::string& channel_id,
                                     int channel_type,
                                     int64_t before_timestamp,
                                     int32_t limit);

  /// @brief Get history with cursor-based pagination
  std::vector<MessageData> GetHistoryV2(const std::string& channel_id,
                                       int channel_type,
                                       const std::string& cursor,
                                       int32_t limit,
                                       std::string* next_cursor);

  /// @brief Enqueue an offline message for a user (serialized MessageData).
  /// Writes to Redis; when Redis is unavailable the message is kept in an
  /// in-memory fallback so single-node deployments (no Redis) still refill
  /// on login. Returns true only when the message landed in Redis.
  bool AddOfflineMessage(const std::string& user_id,
                         const std::string& serialized);

  /// @brief Removes one offline copy by its exact serialized bytes (late-ack
  /// cleanup after a delivery timed out and was requeued). Returns true when
  /// a copy was found and removed.
  bool RemoveOfflineMessage(const std::string& user_id,
                            const std::string& serialized);

  /// @brief Get offline messages for a user (Redis queue + in-memory fallback)
  std::vector<MessageData> GetOfflineMessages(const std::string& user_id);

  /// @brief Pop offline messages (retrieve and delete)
  std::vector<MessageData> PopOfflineMessages(const std::string& user_id);

  /// @brief Clear offline messages for a user
  bool ClearOfflineMessages(const std::string& user_id);

  /// @brief Track message delivery
  std::string TrackMessage(const std::string& message_id,
                          const std::string& receiver_id,
                          int64_t expires_at);

  /// @brief Acknowledge message delivery
  bool AcknowledgeMessage(const std::string& message_id, const std::string& user_id);

  /// @brief Mark message as failed
  bool FailMessage(const std::string& message_id,
                  const std::string& user_id,
                  const std::string& error);

  /// @brief Get delivery status
  std::optional<DeliveryInfo> GetDeliveryStatus(const std::string& message_id,
                                               const std::string& receiver_id);

  /// @brief Get pending delivery attempts
  std::vector<DeliveryInfo> GetPendingDeliveries(int64_t before_timestamp);

  /// @brief Get Redis client (for migration worker)
  std::shared_ptr<network::RedisClient> GetRedisClient() { return redis_; }

  /// @brief Get the archive store (for migration worker)
  std::shared_ptr<MessageStore> GetMySQLStore() { return mysql_store_; }

  /// @brief Get configuration
  const MessageStoreConfig& GetConfig() const { return config_; }

  /// @brief Get private channel ID for 1v1 chat
  static std::string PrivateChannelId(const std::string& a, const std::string& b);

private:
  std::string OfflineKey(const std::string& user_id);
  std::string HistoryKey(const std::string& channel_id);
  std::string DeliveryKey(const std::string& message_id, const std::string& receiver_id);
  std::string PendingDeliveryKey();

  asio::io_context& io_;
  MessageStoreConfig config_;

  // In-memory fallback for the offline queue when Redis is down. Keyed by
  // user_id, holds serialized MessageData payloads; drained together with
  // the Redis queue by Get/Pop/Clear.
  std::mutex offline_fallback_mutex_;
  std::map<std::string, std::deque<std::string>> offline_fallback_;
  std::shared_ptr<network::RedisClient> redis_;
  std::shared_ptr<MySQLConnectionPool> mysql_pool_;
  std::shared_ptr<MessageStore> mysql_store_;
};

} // namespace chirp::chat
