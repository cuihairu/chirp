#include "network/session_registry.h"

namespace chirp::network {

namespace {

// Erases one device slot of a user's device map, dropping the user entry
// entirely when its last device slot goes away. The slot is only cleared
// when it is stale or still points at the given session.
void EraseDeviceSlot(SessionRegistry& state,
                     const std::string& user_id,
                     const std::string& device_id,
                     const Session* session) {
  auto user_it = state.user_to_sessions.find(user_id);
  if (user_it == state.user_to_sessions.end()) {
    return;
  }
  auto& devices = user_it->second;
  auto device_it = devices.find(device_id);
  if (device_it == devices.end()) {
    return;
  }
  auto bound = device_it->second.lock();
  if (!bound || bound.get() == session) {
    devices.erase(device_it);
    if (devices.empty()) {
      state.user_to_sessions.erase(user_it);
    }
  }
}

} // namespace

std::string NormalizeDeviceId(const std::string& device_id) {
  return device_id.empty() ? "default" : device_id;
}

std::shared_ptr<Session> BindAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                                  const std::string& user_id,
                                                  const std::string& session_id,
                                                  const std::string& device_id,
                                                  const std::shared_ptr<Session>& session) {
  const std::string normalized_device = NormalizeDeviceId(device_id);
  std::shared_ptr<Session> old_pair_session;

  std::lock_guard<std::mutex> lock(state->mu);

  // A connection serves a single identity: drop the (user, device) entry it
  // owned before taking the new slot.
  auto old_user_entry = state->session_to_user.find(session.get());
  if (old_user_entry != state->session_to_user.end()) {
    const std::string previous_user_id = old_user_entry->second;
    auto old_device_entry = state->session_to_device.find(session.get());
    const std::string previous_device_id =
        old_device_entry == state->session_to_device.end() ? std::string() : old_device_entry->second;
    if (previous_user_id != user_id || previous_device_id != normalized_device) {
      EraseDeviceSlot(*state, previous_user_id, previous_device_id, session.get());
    }
  }

  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it != state->user_to_sessions.end()) {
    auto device_it = user_it->second.find(normalized_device);
    if (device_it != user_it->second.end()) {
      old_pair_session = device_it->second.lock();
    }
  }

  state->user_to_sessions[user_id][normalized_device] = session;
  state->session_to_user[session.get()] = user_id;
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
  auto it2 = state->session_to_device.find(session.get());
  if (it2 != state->session_to_device.end()) {
    result.device_id = it2->second;
  }
  auto it3 = state->session_to_session_id.find(session.get());
  if (it3 != state->session_to_session_id.end()) {
    result.session_id = it3->second;
  }

  return result;
}

bool RemoveAuthenticatedSession(const std::shared_ptr<SessionRegistry>& state,
                                const std::shared_ptr<Session>& session,
                                std::string* user_id,
                                std::string* device_id) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it == state->session_to_user.end()) {
    return false;
  }

  const std::string current_user_id = it->second;
  auto device_entry = state->session_to_device.find(session.get());
  const std::string current_device_id =
      device_entry == state->session_to_device.end() ? std::string() : device_entry->second;
  state->session_to_user.erase(it);
  state->session_to_device.erase(session.get());
  state->session_to_session_id.erase(session.get());

  // Only report a release when this session owned the slot (or it was
  // stale): a newer session of the same pair keeps its claim.
  bool released = false;
  auto user_it = state->user_to_sessions.find(current_user_id);
  if (user_it != state->user_to_sessions.end()) {
    auto& devices = user_it->second;
    auto device_it = devices.find(current_device_id);
    if (device_it != devices.end()) {
      auto bound = device_it->second.lock();
      if (!bound || bound.get() == session.get()) {
        devices.erase(device_it);
        released = true;
      }
      if (devices.empty()) {
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
                                    const std::string& device_id) {
  const std::string normalized_device = NormalizeDeviceId(device_id);
  std::lock_guard<std::mutex> lock(state->mu);
  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it == state->user_to_sessions.end()) {
    return nullptr;
  }
  auto device_it = user_it->second.find(normalized_device);
  if (device_it == user_it->second.end()) {
    return nullptr;
  }
  return device_it->second.lock();
}

std::shared_ptr<Session> GetAnySession(const std::shared_ptr<SessionRegistry>& state,
                                       const std::string& user_id) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto user_it = state->user_to_sessions.find(user_id);
  if (user_it == state->user_to_sessions.end()) {
    return nullptr;
  }
  for (auto& entry : user_it->second) {
    if (auto session = entry.second.lock()) {
      return session;
    }
  }
  return nullptr;
}

} // namespace chirp::network
