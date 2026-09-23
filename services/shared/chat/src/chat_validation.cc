#include "chat_validation.h"

#include <string>
#include <string_view>

namespace chirp::chat {
namespace {

bool IsAuthenticated(std::string_view authenticated_user_id) {
  return !authenticated_user_id.empty();
}

} // namespace

size_t MaxContentChars(ChannelType type) {
  switch (type) {
    case PRIVATE:
      return 200;
    case WORLD:
      return 100;
    case SYSTEM_CHANNEL:
      return 500;
    default:
      // TEAM / GUILD / MARQUEE are coordination surfaces with no cap.
      return 0;
  }
}

chirp::common::ErrorCode ValidateContentLength(const SendMessageRequest& req) {
  const size_t max_chars = MaxContentChars(req.channel_type());
  if (max_chars == 0) {
    return chirp::common::OK;
  }
  // Count code points: every byte that is not a 0b10xxxxxx UTF-8 continuation
  // byte starts a new character.
  size_t chars = 0;
  for (char c : req.content()) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
      ++chars;
    }
  }
  if (chars > max_chars) {
    return chirp::common::CONTENT_TOO_LONG;
  }
  return chirp::common::OK;
}

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
  if (const chirp::common::ErrorCode length_code = ValidateContentLength(req);
      length_code != chirp::common::OK) {
    return length_code;
  }

  if (req.channel_type() == PRIVATE) {
    if (req.receiver_id().empty() || req.receiver_id() == authenticated_user_id) {
      return chirp::common::INVALID_PARAM;
    }
    // A client-supplied channel_id is tolerated here: the service always
    // derives the canonical private channel from (sender, receiver), so the
    // request field is never trusted. Both the bundled send client and the
    // core SDK fill it in for convenience.
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
