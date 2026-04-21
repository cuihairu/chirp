#include "chat_validation.h"

#include <string>

namespace chirp::chat {
namespace {

bool IsAuthenticated(std::string_view authenticated_user_id) {
  return !authenticated_user_id.empty();
}

} // namespace

bool PrivateChannelContainsUser(std::string_view channel_id, std::string_view user_id) {
  if (channel_id.empty() || user_id.empty()) {
    return false;
  }

  const size_t sep = channel_id.find('|');
  if (sep == std::string_view::npos || sep == 0 || sep + 1 >= channel_id.size()) {
    return false;
  }

  const std::string_view left = channel_id.substr(0, sep);
  const std::string_view right = channel_id.substr(sep + 1);
  return left == user_id || right == user_id;
}

chirp::common::ErrorCode ValidateSendMessageRequest(const SendMessageRequest& req,
                                                    std::string_view authenticated_user_id) {
  if (!IsAuthenticated(authenticated_user_id)) {
    return chirp::common::AUTH_FAILED;
  }
  if (req.sender_id().empty() || req.sender_id() != authenticated_user_id) {
    return chirp::common::AUTH_FAILED;
  }

  if (req.channel_type() == PRIVATE) {
    if (req.receiver_id().empty() || req.receiver_id() == authenticated_user_id) {
      return chirp::common::INVALID_PARAM;
    }
    if (!req.channel_id().empty()) {
      return chirp::common::INVALID_PARAM;
    }
    return chirp::common::OK;
  }

  if (req.channel_id().empty() || !req.receiver_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateGetHistoryRequest(const GetHistoryRequest& req,
                                                   std::string_view authenticated_user_id) {
  if (!IsAuthenticated(authenticated_user_id) || req.user_id().empty() || req.user_id() != authenticated_user_id) {
    return chirp::common::AUTH_FAILED;
  }

  if (req.channel_type() == PRIVATE) {
    if (!PrivateChannelContainsUser(req.channel_id(), authenticated_user_id)) {
      return chirp::common::AUTH_FAILED;
    }
    return chirp::common::OK;
  }

  if (req.channel_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateLogoutRequest(const chirp::auth::LogoutRequest& req,
                                               std::string_view authenticated_user_id,
                                               std::string_view authenticated_session_id) {
  if (!IsAuthenticated(authenticated_user_id)) {
    return chirp::common::AUTH_FAILED;
  }
  if (req.user_id().empty() || req.user_id() != authenticated_user_id) {
    return chirp::common::AUTH_FAILED;
  }
  if (!req.session_id().empty() && req.session_id() != authenticated_session_id) {
    return chirp::common::SESSION_EXPIRED;
  }
  return chirp::common::OK;
}

} // namespace chirp::chat
