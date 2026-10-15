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

std::vector<ChannelType> ParseChannelTypeList(std::string_view csv) {
  std::vector<ChannelType> types;
  size_t pos = 0;
  // Bounded by pos < size so a trailing comma terminates the loop through the
  // condition (the in-loop break only fires on the last token).
  while (pos < csv.size()) {
    const size_t comma = csv.find(',', pos);
    const size_t end = comma == std::string_view::npos ? csv.size() : comma;
    const std::string_view token = csv.substr(pos, end - pos);
    std::string normalized(token);
    // Trim surrounding whitespace, then case-fold ASCII so "GUILD" and
    // " guild " behave like "guild".
    const size_t first = normalized.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) {
      const size_t last = normalized.find_last_not_of(" \t\r\n");
      normalized = normalized.substr(first, last - first + 1);
      for (char& c : normalized) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      if (normalized == "private") {
        types.push_back(PRIVATE);
      } else if (normalized == "team") {
        types.push_back(TEAM);
      } else if (normalized == "guild") {
        types.push_back(GUILD);
      } else if (normalized == "world") {
        types.push_back(WORLD);
      } else if (normalized == "system" || normalized == "system_channel") {
        types.push_back(SYSTEM_CHANNEL);
      } else if (normalized == "marquee") {
        types.push_back(MARQUEE);
      }
      // Unknown tokens are dropped: a typo narrows the allowlist rather than
      // widening it.
    }
    if (comma == std::string_view::npos) {
      break;
    }
    pos = comma + 1;
  }
  return types;
}

bool ChannelTypeInList(const std::vector<ChannelType>& list, ChannelType type) {
  for (const ChannelType candidate : list) {
    if (candidate == type) {
      return true;
    }
  }
  return false;
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

chirp::common::ErrorCode ValidateCoordMetadata(const SendMessageRequest& req) {
  if (req.msg_type() != COORD_LINK) {
    return chirp::common::OK;
  }
  CoordMetadata coord;
  // ParseFromString 对空串返回 true（空消息合法），所以 map_id 空判覆盖
  // 「没带 metadata」与「metadata 解析不出」两种输入。
  if (!coord.ParseFromString(req.metadata()) || coord.map_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateItemMetadata(const SendMessageRequest& req) {
  if (req.msg_type() != ITEM_LINK) {
    return chirp::common::OK;
  }
  ItemMetadata item;
  // 空串同上：item_id 空判同时覆盖「没带 metadata」与「解析不出」。
  if (!item.ParseFromString(req.metadata()) || item.item_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateAchievementMetadata(const SendMessageRequest& req) {
  if (req.msg_type() != ACHIEVEMENT) {
    return chirp::common::OK;
  }
  AchievementMetadata achievement;
  // 空串同上：achievement_id 空判同时覆盖「没带 metadata」与「解析不出」。
  if (!achievement.ParseFromString(req.metadata()) ||
      achievement.achievement_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateTradeMetadata(const SendMessageRequest& req) {
  if (req.msg_type() != TRADE_STATUS) {
    return chirp::common::OK;
  }
  TradeMetadata trade;
  // 空串同上：trade_id 空判同时覆盖「没带 metadata」与「解析不出」。
  // status 是展示文本（proto 注释约定 CREATED/ACCEPTED/COMPLETED/CANCELLED），
  // 门不锁枚举——新状态值不应被旧服务端挡下，与「不越权校验未启用语义」同口径。
  if (!trade.ParseFromString(req.metadata()) || trade.trade_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateRecruitMetadata(const SendMessageRequest& req) {
  // 公会/组队招募同门：各自按对应 Metadata 解析且 id 非空（空判同时覆盖
  // 「没带 metadata」与「解析不出」），其余 msg_type 原样透传。
  if (req.msg_type() == GUILD_RECRUIT) {
    GuildRecruitMetadata guild;
    if (!guild.ParseFromString(req.metadata()) || guild.guild_id().empty()) {
      return chirp::common::INVALID_PARAM;
    }
  } else if (req.msg_type() == PARTY_RECRUIT) {
    PartyRecruitMetadata party;
    if (!party.ParseFromString(req.metadata()) || party.party_id().empty()) {
      return chirp::common::INVALID_PARAM;
    }
  }
  return chirp::common::OK;
}

chirp::common::ErrorCode ValidateSkillMetadata(const SendMessageRequest& req) {
  if (req.msg_type() != SKILL_LINK) {
    return chirp::common::OK;
  }
  SkillMetadata skill;
  // 空串同上：skill_id 空判同时覆盖「没带 metadata」与「解析不出」。
  if (!skill.ParseFromString(req.metadata()) || skill.skill_id().empty()) {
    return chirp::common::INVALID_PARAM;
  }
  return chirp::common::OK;
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
  // 坐标链接的载荷校验与正文长度同层：垃圾/缺 map_id 的 metadata 在落任何
  // 存储之前就拦下，不留「坐标卡片但坐标是空的」幽灵消息。
  if (const chirp::common::ErrorCode coord_code = ValidateCoordMetadata(req);
      coord_code != chirp::common::OK) {
    return coord_code;
  }
  // 物品链接（P0 ITEM_LINK）与坐标链接同层：垃圾/缺 item_id 的 metadata
  // 同样在落任何存储之前拦下。
  if (const chirp::common::ErrorCode item_code = ValidateItemMetadata(req);
      item_code != chirp::common::OK) {
    return item_code;
  }
  // 成就分享（P1 ACHIEVEMENT）与坐标/物品链接同层：垃圾/缺 achievement_id
  // 的 metadata 同样在落任何存储之前拦下。
  if (const chirp::common::ErrorCode achievement_code = ValidateAchievementMetadata(req);
      achievement_code != chirp::common::OK) {
    return achievement_code;
  }
  // 交易状态（P1 TRADE_STATUS）与成就分享同层：垃圾/缺 trade_id 的 metadata
  // 同样在落任何存储之前拦下。
  if (const chirp::common::ErrorCode trade_code = ValidateTradeMetadata(req);
      trade_code != chirp::common::OK) {
    return trade_code;
  }
  // 招募卡片（P1 GUILD_RECRUIT/PARTY_RECRUIT）同层：垃圾/缺 id 的 metadata
  // 同样在落任何存储之前拦下。
  if (const chirp::common::ErrorCode recruit_code = ValidateRecruitMetadata(req);
      recruit_code != chirp::common::OK) {
    return recruit_code;
  }
  // 技能链接（P1 SKILL_LINK）同层：垃圾/缺 skill_id 的 metadata 同样在落
  // 任何存储之前拦下。
  if (const chirp::common::ErrorCode skill_code = ValidateSkillMetadata(req);
      skill_code != chirp::common::OK) {
    return skill_code;
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
