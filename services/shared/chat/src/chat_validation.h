#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"

namespace chirp::chat {

bool PrivateChannelContainsUser(std::string_view channel_id, std::string_view user_id);

// Parses a comma-separated channel-type list ("private,guild") for the CLI
// flags that take a channel allowlist. Tokens are trimmed and case-folded;
// unknown tokens are dropped (a typo therefore narrows the list instead of
// silently widening it). An empty list parses to an empty vector, which the
// callers treat as "nothing allowed" (fail-closed).
std::vector<ChannelType> ParseChannelTypeList(std::string_view csv);

// True when `type` is in `list`; an empty list matches nothing.
bool ChannelTypeInList(const std::vector<ChannelType>& list, ChannelType type);

// Per-channel content cap in Unicode code points (what a player perceives as
// "characters"), not UTF-8 bytes: CJK text is 3 bytes per char. 0 = uncapped.
size_t MaxContentChars(ChannelType type);

// Rejects over-long content with CONTENT_TOO_LONG. Called inside
// ValidateSendMessageRequest, so both binary forms enforce it.
chirp::common::ErrorCode ValidateContentLength(const SendMessageRequest& req);

chirp::common::ErrorCode ValidateSendMessageRequest(const SendMessageRequest& req,
                                                    std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateGetHistoryRequest(const GetHistoryRequest& req,
                                                   std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateLogoutRequest(const chirp::auth::LogoutRequest& req,
                                               std::string_view authenticated_user_id,
                                               std::string_view authenticated_session_id);

} // namespace chirp::chat
