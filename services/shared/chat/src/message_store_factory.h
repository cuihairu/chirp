#pragma once

#include <memory>

#include "message_store.h"
#include "message_store_config.h"

namespace chirp {
namespace chat {

class MySQLConnectionPool;

/// @brief The single seam where the concrete archive backend is chosen.
/// Consumers (HybridMessageStore, tests) depend only on the MessageStore
/// interface; adding a PostgreSQL backend later means adding a branch here,
/// not touching any call site.
std::shared_ptr<MySQLConnectionPool> MakeConnectionPool(const MessageStoreConfig& config);
std::shared_ptr<MessageStore> MakeMessageStore(std::shared_ptr<MySQLConnectionPool> pool);

} // namespace chat
} // namespace chirp
