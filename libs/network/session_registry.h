#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "network/session.h"

namespace chirp::network {

/// @brief Registry of authenticated user <-> session bindings, shared by the
/// connection-facing services (gateway, app_gateway, chat). A session is
/// bound after successful login; rebinding the same user kicks the previous
/// session, and rebinding the same connection drops its previous user.
struct SessionRegistry {
  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;
  std::unordered_map<void*, std::string> session_to_session_id;
};

/// @brief The authenticated identity recorded for a session.
struct AuthenticatedSession {
  std::string user_id;
  std::string session_id;
};

/// @brief Binds a session to a user, returning the session previously bound
/// to that user (if any) so the caller can kick it.
std::shared_ptr<Session> BindAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const std::string& session_id,
                                                  const std::shared_ptr<Session>& session);

/// @brief Looks up the authenticated identity recorded for a session.
AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                             const std::shared_ptr<Session>& session);

/// @brief Removes a session's registration. Returns false when the session
/// was never bound; otherwise true and, when user_id is given, the user the
/// session was bound to (for the caller's cross-instance release logic).
bool RemoveAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                const std::shared_ptr<Session>& session,
                                std::string* user_id = nullptr);

} // namespace chirp::network
