#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio.hpp>

#include "message_store_config.h"
#include "mysql_message_store.h"
#include "network/redis_client.h"

namespace chirp::chat {

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

/// @brief Delivery status for a message
enum class DeliveryStatus {
  kPending = 0,
  kDelivered = 1,
  kFailed = 2,
  kAcknowledged = 3
};

/// @brief Delivery tracking info
struct DeliveryInfo {
  std::string message_id;
  std::string receiver_id;
  DeliveryStatus status{DeliveryStatus::kPending};
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
                                             DeliveryStatus status)>;

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

  /// @brief Get offline messages for a user
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

  /// @brief Get MySQL store (for migration worker)
  std::shared_ptr<MySQLMessageStore> GetMySQLStore() { return mysql_store_; }

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
  std::shared_ptr<network::RedisClient> redis_;
  std::shared_ptr<MySQLConnectionPool> mysql_pool_;
  std::shared_ptr<MySQLMessageStore> mysql_store_;
};

} // namespace chirp::chat
