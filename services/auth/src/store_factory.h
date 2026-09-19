#pragma once

#include <memory>

#include "session_store.h"
#include "user_store.h"

namespace chirp::auth {

/// @brief The single seam where the concrete storage backend is chosen.
/// Consumers (AuthService, tests) depend only on the UserStore/SessionStore
/// interfaces; adding a PostgreSQL backend later means adding a branch here,
/// not touching any call site.
std::shared_ptr<UserStore> MakeUserStore(const UserStore::Config& config);
std::shared_ptr<SessionStore> MakeSessionStore(const SessionStore::Config& config);

} // namespace chirp::auth
