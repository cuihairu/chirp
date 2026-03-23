#include "message_store_config.h"

#include <cstdlib>
#include <cstring>

#include "logger.h"

namespace chirp::chat {

using chirp::common::Logger;

MessageStoreConfig MessageStoreConfig::FromEnv() {
  MessageStoreConfig config;

  const char* env_val;

  if ((env_val = std::getenv("CHIRP_REDIS_HOST"))) config.redis_host = env_val;
  if ((env_val = std::getenv("CHIRP_REDIS_PORT")))
    config.redis_port = static_cast<uint16_t>(std::atoi(env_val));

  if ((env_val = std::getenv("CHIRP_MYSQL_HOST"))) config.mysql_host = env_val;
  if ((env_val = std::getenv("CHIRP_MYSQL_PORT")))
    config.mysql_port = static_cast<uint16_t>(std::atoi(env_val));
  if ((env_val = std::getenv("CHIRP_MYSQL_DATABASE"))) config.mysql_database = env_val;
  if ((env_val = std::getenv("CHIRP_MYSQL_USER"))) config.mysql_user = env_val;
  if ((env_val = std::getenv("CHIRP_MYSQL_PASSWORD"))) config.mysql_password = env_val;

  if ((env_val = std::getenv("CHIRP_MIGRATION_ENABLED")))
    config.enable_migration = (std::string(env_val) == "1" || std::string(env_val) == "true");
  if ((env_val = std::getenv("CHIRP_MIGRATION_BATCH_SIZE")))
    config.migration_batch_size = std::atoi(env_val);

  if ((env_val = std::getenv("CHIRP_DELIVERY_TRACKING_ENABLED")))
    config.enable_delivery_tracking = (std::string(env_val) == "1" || std::string(env_val) == "true");

  return config;
}

bool MessageStoreConfig::Validate() const {
  if (redis_host.empty()) {
    Logger::Instance().Error("MessageStoreConfig: redis_host is required");
    return false;
  }

  if (mysql_host.empty()) {
    Logger::Instance().Error("MessageStoreConfig: mysql_host is required");
    return false;
  }

  if (mysql_database.empty()) {
    Logger::Instance().Error("MessageStoreConfig: mysql_database is required");
    return false;
  }

  if (migration_batch_size <= 0 || migration_batch_size > 10000) {
    Logger::Instance().Error("MessageStoreConfig: invalid migration_batch_size");
    return false;
  }

  return true;
}

} // namespace chirp::chat
