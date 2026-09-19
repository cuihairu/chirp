#include "message_store_factory.h"

#include "mysql_message_store.h"

namespace chirp {
namespace chat {

std::shared_ptr<MySQLConnectionPool> MakeConnectionPool(const MessageStoreConfig& config) {
  return std::make_shared<MySQLConnectionPool>(
      config.mysql_pool_size,
      config.mysql_host,
      config.mysql_port,
      config.mysql_database,
      config.mysql_user,
      config.mysql_password);
}

std::shared_ptr<MessageStore> MakeMessageStore(std::shared_ptr<MySQLConnectionPool> pool) {
  return std::make_shared<MySQLMessageStore>(std::move(pool));
}

} // namespace chat
} // namespace chirp
