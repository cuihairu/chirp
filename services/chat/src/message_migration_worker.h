#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>

#include "chat/src/hybrid_message_store.h"
#include "chat/src/message_store_config.h"

namespace chirp::chat {

/// @brief Background worker that migrates messages from Redis to MySQL
/// Ensures messages stored temporarily in Redis are persisted to MySQL
class MessageMigrationWorker {
public:
  /// @brief Migration statistics
  struct Stats {
    uint64_t total_migrated{0};
    uint64_t total_failed{0};
    uint64_t batches_processed{0};
    uint64_t messages_in_queue{0};
    int64_t last_migration_time_ms{0};
    int64_t total_migration_time_ms{0};
  };

  explicit MessageMigrationWorker(asio::io_context& io,
                                 std::shared_ptr<HybridMessageStore> store,
                                 const MessageStoreConfig& config);
  ~MessageMigrationWorker();

  /// @brief Start the migration worker
  void Start();

  /// @brief Stop the migration worker
  void Stop();

  /// @brief Trigger an immediate migration run
  void RunMigrationNow();

  /// @brief Get migration statistics
  Stats GetStats() const;

  /// @brief Get configuration
  const MessageStoreConfig& GetConfig() const { return config_; }

private:
  struct MigrationState {
    std::string current_channel;
    int migrated_count{0};
    int failed_count{0};
    int64_t start_time_ms{0};
  };

  void ScheduleNextRun();
  void RunMigration();
  void MigrateChannelHistory(const std::string& channel_id);
  void MigrateOfflineMessages(const std::string& user_id);

  asio::steady_timer timer_;
  asio::io_context& io_;
  std::shared_ptr<HybridMessageStore> store_;
  MessageStoreConfig config_;

  std::atomic<bool> running_{false};
  std::atomic<bool> migrating_{false};

  mutable std::mutex stats_mutex_;
  Stats stats_;
};

} // namespace chirp::chat
