#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "network/session.h"

namespace chirp::chat {

struct ChatState {
  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;
  std::unordered_map<void*, std::string> session_to_session_id;
};

struct AuthenticatedSession {
  std::string user_id;
  std::string session_id;
};

std::shared_ptr<chirp::network::Session> BindAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                                                  const std::string& user_id,
                                                                  const std::string& session_id,
                                                                  const std::shared_ptr<chirp::network::Session>& session);

AuthenticatedSession GetAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                             const std::shared_ptr<chirp::network::Session>& session);

void RemoveAuthenticatedSession(const std::shared_ptr<ChatState>& state,
                                const std::shared_ptr<chirp::network::Session>& session);

} // namespace chirp::chat
