#include "message_migration_worker.h"

#include <mutex>

#include "logger.h"

namespace chirp::chat {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

MessageMigrationWorker::MessageMigrationWorker(asio::io_context& io,
                                              std::shared_ptr<HybridMessageStore> store,
                                              const MessageStoreConfig& config)
    : timer_(io), io_(io), store_(std::move(store)), config_(config) {}

MessageMigrationWorker::~MessageMigrationWorker() {
  Stop();
}

void MessageMigrationWorker::Start() {
  if (running_.load()) {
    return;
  }

  if (!config_.enable_migration) {
    Logger::Instance().Info("MessageMigrationWorker disabled by configuration");
    return;
  }

  running_.store(true);
  Logger::Instance().Info("MessageMigrationWorker started (interval: " +
                         std::to_string(config_.migration_interval_seconds) + "s)");

  // Schedule first run
  ScheduleNextRun();
}

void MessageMigrationWorker::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  timer_.cancel();
  Logger::Instance().Info("MessageMigrationWorker stopped");
}

void MessageMigrationWorker::RunMigrationNow() {
  if (migrating_.load()) {
    Logger::Instance().Warn("Migration already in progress, skipping");
    return;
  }

  asio::post(io_, [this]() {
    RunMigration();
  });
}

MessageMigrationWorker::Stats MessageMigrationWorker::GetStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void MessageMigrationWorker::ScheduleNextRun() {
  if (!running_.load()) {
    return;
  }

  timer_.expires_after(std::chrono::seconds(config_.migration_interval_seconds));
  timer_.async_wait([this](const std::error_code& ec) {
    if (!ec) {
      RunMigration();
    }
  });
}

void MessageMigrationWorker::RunMigration() {
  if (!running_.load() || migrating_.load()) {
    ScheduleNextRun();
    return;
  }

  migrating_.store(true);
  int64_t start_time = NowMs();

  Logger::Instance().Info("Starting message migration batch");

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.last_migration_time_ms = start_time;
  }

  // Migration strategy:
  // 1. Get all channel history keys from Redis
  // 2. For each channel, migrate messages to MySQL
  // 3. Migrate offline message queues to MySQL
  // 4. Clean up migrated messages from Redis

  auto redis = store_->GetRedisClient();

  // Get all history keys
  auto history_keys = redis->Keys("chirp:chat:history:*");

  int batch_migrated = 0;
  int batch_failed = 0;

  for (const auto& key : history_keys) {
    // Extract channel_id from key
    std::string channel_id = key.substr(strlen("chirp:chat:history:"));

    // Get messages from Redis list
    auto messages = redis->LRange(key, 0, -1);

    for (const auto& msg_data : messages) {
      MessageData msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        // Store in MySQL
        MySQLMessageStore::MessageData mysql_msg;
        mysql_msg.message_id = msg.message_id;
        mysql_msg.sender_id = msg.sender_id;
        mysql_msg.receiver_id = msg.receiver_id;
        mysql_msg.channel_id = msg.channel_id;
        mysql_msg.channel_type = msg.channel_type;
        mysql_msg.msg_type = msg.msg_type;
        mysql_msg.content = msg.content;
        mysql_msg.timestamp = msg.timestamp;
        mysql_msg.created_at = msg.created_at;

        if (store_->GetMySQLStore()->StoreMessage(mysql_msg)) {
          batch_migrated++;
        } else {
          batch_failed++;
        }
      }
    }

    // After successful migration, trim Redis list to keep only recent messages
    redis->LTrim(key, -config_.redis_history_limit, -1);
  }

  // Migrate offline messages
  auto offline_keys = redis->Keys("chirp:chat:offline:*");

  for (const auto& key : offline_keys) {
    std::string user_id = key.substr(strlen("chirp:chat:offline:"));

    auto messages = redis->LRange(key, 0, -1);

    for (const auto& msg_data : messages) {
      MessageData msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        MySQLMessageStore::MessageData mysql_msg;
        mysql_msg.message_id = msg.message_id;
        mysql_msg.sender_id = msg.sender_id;
        mysql_msg.receiver_id = msg.receiver_id;
        mysql_msg.channel_id = msg.channel_id;
        mysql_msg.channel_type = msg.channel_type;
        mysql_msg.msg_type = msg.msg_type;
        mysql_msg.content = msg.content;
        mysql_msg.timestamp = msg.timestamp;
        mysql_msg.created_at = msg.created_at;

        // Ensure receiver_id is set for offline messages
        if (mysql_msg.receiver_id.empty()) {
          mysql_msg.receiver_id = user_id;
        }

        if (store_->GetMySQLStore()->StoreMessage(mysql_msg)) {
          batch_migrated++;
        } else {
          batch_failed++;
        }
      }
    }
  }

  int64_t duration_ms = NowMs() - start_time;

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_migrated += batch_migrated;
    stats_.total_failed += batch_failed;
    stats_.batches_processed++;
    stats_.total_migration_time_ms += duration_ms;
    stats_.messages_in_queue = 0;  // Approximate, could be calculated
  }

  Logger::Instance().Info("Migration batch completed: " +
                         std::to_string(batch_migrated) + " migrated, " +
                         std::to_string(batch_failed) + " failed, " +
                         std::to_string(duration_ms) + "ms");

  migrating_.store(false);
  ScheduleNextRun();
}

void MessageMigrationWorker::MigrateChannelHistory(const std::string& channel_id) {
  // Implementation for specific channel migration
  // This would be called for targeted migrations
}

void MessageMigrationWorker::MigrateOfflineMessages(const std::string& user_id) {
  // Implementation for specific user offline message migration
  // This would be called when user comes online
}

} // namespace chirp::chat
