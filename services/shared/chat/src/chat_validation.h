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

// 坐标链接（P1 COORD_LINK）：metadata 必须能按 CoordMetadata 解析且 map_id
// 非空，否则 INVALID_PARAM。只对 COORD_LINK 生效——其他 msg_type 的 metadata
// 原样透传（协议层不越权校验未启用消息类型的载荷）。
chirp::common::ErrorCode ValidateCoordMetadata(const SendMessageRequest& req);

// 物品链接（P0 ITEM_LINK）：metadata 必须能按 ItemMetadata 解析且 item_id
// 非空，否则 INVALID_PARAM。与坐标链接同层同一口径——其余 msg_type 的
// metadata 原样透传。
chirp::common::ErrorCode ValidateItemMetadata(const SendMessageRequest& req);

// 成就分享（P1 ACHIEVEMENT）：metadata 必须能按 AchievementMetadata 解析且
// achievement_id 非空，否则 INVALID_PARAM。与坐标/物品链接同层同口径。
chirp::common::ErrorCode ValidateAchievementMetadata(const SendMessageRequest& req);

chirp::common::ErrorCode ValidateSendMessageRequest(const SendMessageRequest& req,
                                                    std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateGetHistoryRequest(const GetHistoryRequest& req,
                                                   std::string_view authenticated_user_id);

chirp::common::ErrorCode ValidateLogoutRequest(const chirp::auth::LogoutRequest& req,
                                               std::string_view authenticated_user_id,
                                               std::string_view authenticated_session_id);

} // namespace chirp::chat
