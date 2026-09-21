#pragma once

#include <string_view>

#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"

namespace chirp::chat {

bool PrivateChannelContainsUser(std::string_view channel_id, std::string_view user_id);

chirp::common::ErrorCode ValidateSendMessageRequest(const SendMessageRequest& req,
                                                    std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateGetHistoryRequest(const GetHistoryRequest& req,
                                                   std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateLogoutRequest(const chirp::auth::LogoutRequest& req,
                                               std::string_view authenticated_user_id,
                                               std::string_view authenticated_session_id);

} // namespace chirp::chat
