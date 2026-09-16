#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "network/session.h"

namespace chirp::network {

/// @brief Registry of authenticated (user, device) <-> session bindings,
/// shared by the connection-facing services (gateway, app_gateway, chat).
/// A session is bound after successful login. The session identity is the
/// (user_id, device_id) pair: rebinding the same pair kicks the previous
/// session, while another device of the same user coexists. Rebinding the
/// same connection drops its previous binding. Device ids are normalized
/// (NormalizeDeviceId) before entering the registry, so legacy clients that
/// never send one all share the "default" device and keep the historical
/// one-session-per-user behavior.
struct SessionRegistry {
  std::mutex mu;
  // user_id -> device_id -> session (one slot per (user, device) pair).
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::weak_ptr<Session>>>
      user_to_sessions;
  std::unordered_map<void*, std::string> session_to_user;
  std::unordered_map<void*, std::string> session_to_device;
  std::unordered_map<void*, std::string> session_to_session_id;
};

/// @brief The authenticated identity recorded for a session.
struct AuthenticatedSession {
  std::string user_id;
  std::string device_id;
  std::string session_id;
};

/// @brief Device ids normalize to "default" when empty so legacy clients
/// that do not send one all contend for a single slot per user.
std::string NormalizeDeviceId(const std::string& device_id);

/// @brief Binds a session to (user_id, device_id), returning the session
/// previously bound to the same pair (if any) so the caller can kick it.
/// The device id is normalized inside (NormalizeDeviceId), so callers may
/// pass the raw login field. Binding another device of the same user
/// returns null and leaves the other devices untouched.
std::shared_ptr<Session> BindAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const std::string& session_id,
                                                  const std::string& device_id,
                                                  const std::shared_ptr<Session>& session);

/// @brief Looks up the authenticated identity recorded for a session.
AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                             const std::shared_ptr<Session>& session);

/// @brief Removes a session's registration. Returns true only when the
/// session's own (user, device) slot was released - it pointed at this
/// session or was stale; a newer session of the same pair keeps its claim.
/// Callers use the result to decide whether to release the cross-instance
/// claim. When user_id/device_id are given they receive the identity the
/// session was bound to regardless of the return value.
bool RemoveAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                const std::shared_ptr<Session>& session,
                                std::string* user_id = nullptr,
                                std::string* device_id = nullptr);

/// @brief Snapshot of every live session bound to user_id (one shared_ptr
/// per device); callers deliver outside the registry lock.
std::vector<std::shared_ptr<Session>> GetUserSessions(const std::shared_ptr<SessionRegistry>& state,
                                                      const std::string& user_id);

/// @brief Exact (user_id, device_id) lookup, e.g. for cross-instance kicks.
std::shared_ptr<Session> GetSession(const std::shared_ptr<SessionRegistry>& state,
                                    const std::string& user_id,
                                    const std::string& device_id);

/// @brief Transitional helper: any live session of user_id, preserving the
/// single-delivery behavior of the pre-device registry. Delivery paths
/// switch to GetUserSessions fan-out one build at a time; this goes away
/// when the last one does.
std::shared_ptr<Session> GetAnySession(const std::shared_ptr<SessionRegistry>& state,
                                       const std::string& user_id);

} // namespace chirp::network
