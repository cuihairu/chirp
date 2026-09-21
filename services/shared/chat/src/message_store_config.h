#pragma once

#include <cstdint>
#include <string>

namespace chirp::chat {

/// @brief Configuration for message storage
/// Centralized configuration for hybrid Redis+MySQL storage
struct MessageStoreConfig {
  // Redis configuration
  std::string redis_host = "127.0.0.1";
  uint16_t redis_port = 6379;
  int redis_history_limit = 100;         // Max messages per channel in Redis
  int redis_offline_ttl_seconds = 604800;  // 7 days for offline messages

  // MySQL configuration
  std::string mysql_host = "127.0.0.1";
  uint16_t mysql_port = 3306;
  std::string mysql_database = "chirp";
  std::string mysql_user = "chirp";
  std::string mysql_password = "chirp_password";
  size_t mysql_pool_size = 10;

  // Migration configuration
  bool enable_migration = true;
  int migration_batch_size = 100;        // Messages per batch
  int migration_interval_seconds = 30;   // Run migration every N seconds
  int migration_max_retries = 3;

  // Delivery tracking
  bool enable_delivery_tracking = true;
  int delivery_retry_interval_seconds = 5;
  int delivery_max_retries = 5;
  int64_t delivery_timeout_seconds = 300;  // 5 minutes

  // History retrieval
  int default_history_limit = 50;
  int max_history_limit = 500;
  int pagination_page_size = 50;

  // ACK/NACK
  bool require_ack = false;  // If true, messages must be ACKed
  int64_t ack_timeout_seconds = 60;

  /// @brief Load from environment variables
  static MessageStoreConfig FromEnv();

  /// @brief Validate configuration
  bool Validate() const;
};

} // namespace chirp::chat
