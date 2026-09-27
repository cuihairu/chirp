#include "network/session_registry.h"

namespace chirp::network {

namespace {

// Erases one platform slot of a user's platform map, dropping the user
// entry entirely when its last platform slot goes away. The slot is only
// cleared when it is stale or still points at the given session.
void ErasePlatformSlot(SessionRegistry& state,
                       const std::string& user_id,
                       const std::string& platform,
                       const Session* session) {
  auto user_it = state.user_to_sessions.find(user_id);
  if (user_it == state.user_to_sessions.end()) {
    return;
  }
  auto& platforms = user_it->second;
  auto platform_it = platforms.find(platform);
  if (platform_it == platforms.end()) {
    return;
  }
  auto bound = platform_it->second.lock();
  if (!bound || bound.get() == session) {
    platforms.erase(platform_it);
    if (platforms.empty()) {
      state.user_to_sessions.erase(user_it);
    }
  }
}

} // namespace

std::string NormalizeDeviceId(const std::string& device_id) {
  return device_id.empty() ? "default" : device_id;
}

std::string NormalizePlatformId(const std::string& platform) {
  return platform.empty() ? "default" : platform;
}

std::shared_ptr<Session> BindAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const std::string& session_id,
                                                  const std::string& device_id,
                                                  const std::shared_ptr<Session>& session,
                                                  const std::string& platform) {
  const std::string normalized_device = NormalizeDeviceId(device_id);
  const std::string normalized_platform = NormalizePlatformId(platform);
  std::shared_ptr<Session> old_pair_session;

  std::lock_guard<std::mutex> lock(state->mu);

  // A connection serves a single identity: drop the (user, platform) entry
  // it owned before taking the new slot.
  auto old_user_entry = state->session_to_user.find(session.get());
  if (old_user_entry != state->session_to_user.end()) {
    const std::string previous_user_id = old_user_entry->second;
    auto old_platform_entry = state->session_to_platform.find(session.get());
    const std::string previous_platform =
        old_platform_entry == state->session_to_platform.end() ? std::string()
                                                               : old_platform_entry->second;
    if (previous_user_id != user_id || previous_platform != normalized_platform) {
      ErasePlatformSlot(*state, previous_user_id, previous_platform, session.get());
    }
  }

  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it != state->user_to_sessions.end()) {
    auto platform_it = user_it->second.find(normalized_platform);
    if (platform_it != user_it->second.end()) {
      old_pair_session = platform_it->second.lock();
    }
  }

  state->user_to_sessions[user_id][normalized_platform] = session;
  state->session_to_user[session.get()] = user_id;
  state->session_to_platform[session.get()] = normalized_platform;
  state->session_to_device[session.get()] = normalized_device;
  state->session_to_session_id[session.get()] = session_id;

  return old_pair_session;
}

AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                             const std::shared_ptr<Session>& session) {
  AuthenticatedSession result;

  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it != state->session_to_user.end()) {
    result.user_id = it->second;
  }
  auto it2 = state->session_to_platform.find(session.get());
  if (it2 != state->session_to_platform.end()) {
    result.platform = it2->second;
  }
  auto it3 = state->session_to_device.find(session.get());
  if (it3 != state->session_to_device.end()) {
    result.device_id = it3->second;
  }
  auto it4 = state->session_to_session_id.find(session.get());
  if (it4 != state->session_to_session_id.end()) {
    result.session_id = it4->second;
  }

  return result;
}

bool RemoveAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                const std::shared_ptr<Session>& session,
                                std::string* user_id,
                                std::string* device_id,
                                std::string* platform) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it == state->session_to_user.end()) {
    return false;
  }

  const std::string current_user_id = it->second;
  auto platform_entry = state->session_to_platform.find(session.get());
  const std::string current_platform =
      platform_entry == state->session_to_platform.end() ? std::string() : platform_entry->second;
  auto device_entry = state->session_to_device.find(session.get());
  const std::string current_device_id =
      device_entry == state->session_to_device.end() ? std::string() : device_entry->second;
  state->session_to_user.erase(it);
  state->session_to_platform.erase(session.get());
  state->session_to_device.erase(session.get());
  state->session_to_session_id.erase(session.get());

  // Only report a release when this session owned the slot (or it was
  // stale): a newer session of the same pair keeps its claim.
  bool released = false;
  auto user_it = state->user_to_sessions.find(current_user_id);
  if (user_it != state->user_to_sessions.end()) {
    auto& platforms = user_it->second;
    auto platform_it = platforms.find(current_platform);
    if (platform_it != platforms.end()) {
      auto bound = platform_it->second.lock();
      if (!bound || bound.get() == session.get()) {
        platforms.erase(platform_it);
        released = true;
      }
      if (platforms.empty()) {
        state->user_to_sessions.erase(user_it);
      }
    }
  }

  if (user_id) {
    *user_id = current_user_id;
  }
  if (device_id) {
    *device_id = current_device_id;
  }
  if (platform) {
    *platform = current_platform;
  }
  return released;
}

std::vector<std::shared_ptr<Session>> GetUserSessions(const std::shared_ptr<SessionRegistry>& state,
                                                      const std::string& user_id) {
  std::vector<std::shared_ptr<Session>> sessions;

  std::lock_guard<std::mutex> lock(state->mu);
  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it == state->user_to_sessions.end()) {
    return sessions;
  }
  sessions.reserve(user_it->second.size());
  for (auto& entry : user_it->second) {
    if (auto session = entry.second.lock()) {
      sessions.push_back(std::move(session));
    }
  }
  return sessions;
}

std::shared_ptr<Session> GetSession(const std::shared_ptr<SessionRegistry>& state,
                                    const std::string& user_id,
                                    const std::string& platform) {
  const std::string normalized_platform = NormalizePlatformId(platform);
  std::lock_guard<std::mutex> lock(state->mu);
  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it == state->user_to_sessions.end()) {
    return nullptr;
  }
  auto platform_it = user_it->second.find(normalized_platform);
  if (platform_it == user_it->second.end()) {
    return nullptr;
  }
  return platform_it->second.lock();
}

std::vector<DevicePresenceInfo> ListOnlineDevices(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const Session* exclude) {
  std::vector<DevicePresenceInfo> devices;

  std::lock_guard<std::mutex> lock(state->mu);
  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it == state->user_to_sessions.end()) {
    return devices;
  }
  for (const auto& entry : user_it->second) {
    const auto bound = entry.second.lock();
    if (!bound || bound.get() == exclude) {
      continue;
    }
    DevicePresenceInfo info;
    info.platform = entry.first;
    auto device_it = state->session_to_device.find(bound.get());
    if (device_it != state->session_to_device.end()) {
      info.device_id = device_it->second;
    }
    devices.push_back(std::move(info));
  }
  return devices;
}

} // namespace chirp::network
