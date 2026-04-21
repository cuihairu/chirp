#include "chat_session_registry.h"

namespace chirp::chat {

std::shared_ptr<chirp::network::Session> BindAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                                                  const std::string& user_id,
                                                                  const std::string& session_id,
                                                                  const std::shared_ptr<chirp::network::Session>& session) {
  std::shared_ptr<chirp::network::Session> old_user_session;
  std::string previous_user_id;

  std::lock_guard<std::mutex> lock(state->mu);

  auto old_session_entry = state->session_to_user.find(session.get());
  if (old_session_entry != state->session_to_user.end()) {
    previous_user_id = old_session_entry->second;
    if (previous_user_id != user_id) {
      auto old_user_entry = state->user_to_session.find(previous_user_id);
      if (old_user_entry != state->user_to_session.end()) {
        auto bound = old_user_entry->second.lock();
        if (!bound || bound.get() == session.get()) {
          state->user_to_session.erase(old_user_entry);
        }
      }
    }
  }

  auto existing_user_entry = state->user_to_session.find(user_id);
  if (existing_user_entry != state->user_to_session.end()) {
    old_user_session = existing_user_entry->second.lock();
  }

  state->user_to_session[user_id] = session;
  state->session_to_user[session.get()] = user_id;
  state->session_to_session_id[session.get()] = session_id;

  return old_user_session;
}

AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                             const std::shared_ptr<chirp::network::Session>& session) {
  AuthenticatedSession result;

  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it != state->session_to_user.end()) {
    result.user_id = it->second;
  }
  auto it2 = state->session_to_session_id.find(session.get());
  if (it2 != state->session_to_session_id.end()) {
    result.session_id = it2->second;
  }

  return result;
}

void RemoveAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                const std::shared_ptr<chirp::network::Session>& session) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it == state->session_to_user.end()) {
    return;
  }

  const std::string user_id = it->second;
  state->session_to_user.erase(it);
  state->session_to_session_id.erase(session.get());

  auto it2 = state->user_to_session.find(user_id);
  if (it2 != state->user_to_session.end()) {
    auto cur = it2->second.lock();
    if (!cur || cur.get() == session.get()) {
      state->user_to_session.erase(it2);
    }
  }
}

} // namespace chirp::chat
