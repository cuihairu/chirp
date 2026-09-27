#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "network/session.h"

namespace chirp::network {

/// @brief Registry of authenticated (user, platform) <-> session bindings,
/// shared by the connection-facing services (gateway, app_gateway, chat).
/// A session is bound after successful login. The session identity is the
/// (user_id, platform) pair: rebinding the same pair kicks the previous
/// session (多端在线顶号：同 platform 新登录顶掉旧会话，跨 platform 共存),
/// while another platform of the same user coexists. Rebinding the same
/// connection drops its previous binding. The same device reconnecting
/// keeps its idempotent rebind semantics: it carries the same platform, so
/// it lands on the same slot and displaces only the dead previous session.
/// Device ids and platform strings are normalized (NormalizeDeviceId /
/// NormalizePlatformId) before entering the registry, so legacy clients
/// that never send one all share the "default" slot and keep the
/// historical one-session-per-user behavior. device_id is still recorded
/// per session as metadata (online-device listings report it).
struct SessionRegistry {
  std::mutex mu;
  // user_id -> platform -> session (one slot per (user, platform) pair).
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::weak_ptr<Session>>>
      user_to_sessions;
  std::unordered_map<void*, std::string> session_to_user;
  std::unordered_map<void*, std::string> session_to_platform;
  std::unordered_map<void*, std::string> session_to_device;
  std::unordered_map<void*, std::string> session_to_session_id;
};

/// @brief The authenticated identity recorded for a session.
struct AuthenticatedSession {
  std::string user_id;
  std::string platform;
  std::string device_id;
  std::string session_id;
};

/// @brief Device ids normalize to "default" when empty so legacy clients
/// that do not send one all contend for a single slot per user.
std::string NormalizeDeviceId(const std::string& device_id);

/// @brief Platform strings normalize to "default" when empty - legacy
/// clients that do not declare a platform share one slot per user, which
/// keeps the pre-multi-device one-session-per-user behavior for them.
std::string NormalizePlatformId(const std::string& platform);

/// @brief Binds a session to (user_id, platform), returning the session
/// previously bound to the same pair (if any) so the caller can kick it
/// with a "logged in on another <platform>" notice. The platform string is
/// normalized inside (NormalizePlatformId), so callers may pass the raw
/// login field. Binding another platform of the same user returns null and
/// leaves the other platforms untouched.
std::shared_ptr<Session> BindAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const std::string& session_id,
                                                  const std::string& device_id,
                                                  const std::shared_ptr<Session>& session,
                                                  const std::string& platform = "");

/// @brief Looks up the authenticated identity recorded for a session.
AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                             const std::shared_ptr<Session>& session);

/// @brief Removes a session's registration. Returns true only when the
/// session's own (user, platform) slot was released - it pointed at this
/// session or was stale; a newer session of the same pair keeps its claim.
/// Callers use the result to decide whether to release the cross-instance
/// claim (and whether the user went fully offline). When user_id/device_id
/// are given they receive the identity the session was bound to regardless
/// of the return value; platform likewise.
bool RemoveAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                const std::shared_ptr<Session>& session,
                                std::string* user_id = nullptr,
                                std::string* device_id = nullptr,
                                std::string* platform = nullptr);

/// @brief Snapshot of every live session bound to user_id (one shared_ptr
/// per platform); callers deliver outside the registry lock.
std::vector<std::shared_ptr<Session>> GetUserSessions(const std::shared_ptr<SessionRegistry>& state,
                                                      const std::string& user_id);

/// @brief Exact (user_id, platform) lookup, e.g. for cross-instance kicks.
std::shared_ptr<Session> GetSession(const std::shared_ptr<SessionRegistry>& state,
                                    const std::string& user_id,
                                    const std::string& platform);

/// @brief One live (platform, device) binding of a user - the payload shape
/// of the multi-device online listing (proto auth.DevicePresence without
/// the online/ts fields, which describe the event, not the slot).
struct DevicePresenceInfo {
  std::string platform;
  std::string device_id;
};

/// @brief Snapshot of every live (platform, device) binding of user_id,
/// optionally excluding one session (the one that just logged in - it gets
/// the same list in its LoginResponse). Callers deliver outside the
/// registry lock. Entries whose weak_ptr died are skipped, not reported
/// offline: only real transitions (bind/remove) emit events.
std::vector<DevicePresenceInfo> ListOnlineDevices(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const Session* exclude = nullptr);

} // namespace chirp::network
