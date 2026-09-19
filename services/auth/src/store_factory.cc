#include "store_factory.h"

#include "mysql_session_store.h"
#include "mysql_user_store.h"

namespace chirp::auth {

std::shared_ptr<UserStore> MakeUserStore(const UserStore::Config& config) {
  return std::make_shared<MySQLUserStore>(config);
}

std::shared_ptr<SessionStore> MakeSessionStore(const SessionStore::Config& config) {
  return std::make_shared<MySQLSessionStore>(config);
}

} // namespace chirp::auth
