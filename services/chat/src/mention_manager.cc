#include "mention_manager.h"

#include <regex>
#include <sstream>

namespace chirp {
namespace chat {

namespace {

// Regex patterns for parsing mentions
const std::regex kUserMentionRegex(R"(@<([^|]+)\|([^>]+)>)");     // @Username|ID
const std::regex kRoleMentionRegex(R"(@<([^|]+)\|([^>]+)>)");      // @Rolename|ID
const std::regex kChannelMentionRegex(R"(#<([^|]+)\|([^>]+)>)");  // #Channelname|ID
const std::regex kEveryoneRegex(R"(@everyone)");
const std::regex kHereRegex(R"(@here)");

// Simple mention format: @username, #channel
const std::regex kSimpleUserMentionRegex(R"(@(\w{2,32}))");
const std::regex kSimpleChannelMentionRegex(R"(#(\w{2,32}))");

} // namespace

// ParsedMentions implementation
std::unordered_set<std::string> ParsedMentions::GetNotifyUserIds(
    const std::string& sender_id,
    const std::unordered_set<std::string>& channel_member_ids) const {

  std::unordered_set<std::string> result;

  // Add directly mentioned users
  for (const auto& user_id : mentioned_user_ids) {
    if (user_id != sender_id) {  // Don't notify sender
      result.insert(user_id);
    }
  }

  // Add all online users for @here
  if (mentions_here) {
    for (const auto& user_id : channel_member_ids) {
      if (user_id != sender_id) {
        result.insert(user_id);
      }
    }
  }

  // Add all users for @everyone
  if (mentions_everyone) {
    for (const auto& user_id : channel_member_ids) {
      if (user_id != sender_id) {
        result.insert(user_id);
      }
    }
  }

  return result;
}

// MentionManager implementation

MentionManager::MentionManager(const MentionConfig& config)
    : config_(config) {}

int64_t MentionManager::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

ParsedMentions MentionManager::ParseMentions(const std::string& content,
                                             const std::string& sender_id) {
  ParsedMentions result;

  // Parse @mentions
  {
    std::sregex_iterator it(content.begin(), content.end(), kSimpleUserMentionRegex);
    std::sregex_iterator end;

    for (int32_t count = 0; it != end && count < config_.max_mentions_per_message; ++it, ++count) {
      const std::smatch& match = *it;
      Mention mention;
      mention.set_type(MentionType::USER);
      mention.set_id(match[1].str());  // Use username as ID for now
      mention.set_start_index(match.position());
      mention.set_length(match.length());
      result.mentions.push_back(mention);
      result.mentioned_user_ids.insert(match[1].str());
    }
  }

  // Parse #channel mentions
  {
    std::sregex_iterator it(content.begin(), content.end(), kSimpleChannelMentionRegex);
    std::sregex_iterator end;

    for (int32_t count = 0; it != end && count < config_.max_mentions_per_message; ++it, ++count) {
      const std::smatch& match = *it;
      Mention mention;
      mention.set_type(MentionType::CHANNEL);
      mention.set_id(match[1].str());
      mention.set_start_index(match.position());
      mention.set_length(match.length());
      result.mentions.push_back(mention);
    }
  }

  // Parse @everyone
  if (config_.allow_everyone) {
    std::sregex_iterator it(content.begin(), content.end(), kEveryoneRegex);
    std::sregex_iterator end;

    if (it != end) {
      const std::smatch& match = *it;
      Mention mention;
      mention.set_type(MentionType::EVERYONE);
      mention.set_id("");
      mention.set_start_index(match.position());
      mention.set_length(match.length());
      result.mentions.push_back(mention);
      result.mentions_everyone = true;
    }
  }

  // Parse @here
  if (config_.allow_here) {
    std::sregex_iterator it(content.begin(), content.end(), kHereRegex);
    std::sregex_iterator end;

    if (it != end) {
      const std::smatch& match = *it;
      Mention mention;
      mention.set_type(MentionType::HERE);
      mention.set_id("");
      mention.set_start_index(match.position());
      mention.set_length(match.length());
      result.mentions.push_back(mention);
      result.mentions_here = true;
    }
  }

  return result;
}

bool MentionManager::CanMentionEveryone(const std::string& user_id,
                                       const std::string& channel_id,
                                       bool is_moderator) {
  if (is_moderator) {
    return true;  // Mods bypass cooldown
  }

  if (!config_.allow_everyone) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  std::string key = user_id + ":" + channel_id;
  auto it = everyone_cooldowns_.find(key);

  if (it == everyone_cooldowns_.end()) {
    return true;  // Never mentioned
  }

  int64_t elapsed = GetCurrentTimeMs() - it->second;
  return elapsed >= config_.everyone_cooldown_ms;
}

bool MentionManager::CanMentionHere(const std::string& user_id,
                                    const std::string& channel_id,
                                    bool is_moderator) {
  // @here usually has same restrictions as @everyone
  return CanMentionEveryone(user_id, channel_id, is_moderator);
}

void MentionManager::RecordEveryoneMention(const std::string& user_id,
                                          const std::string& channel_id) {
  std::lock_guard<std::mutex> lock(mu_);

  std::string key = user_id + ":" + channel_id;
  everyone_cooldowns_[key] = GetCurrentTimeMs();

  // Cleanup old entries
  int64_t cutoff = GetCurrentTimeMs() - (config_.everyone_cooldown_ms * 10);

  for (auto it = everyone_cooldowns_.begin(); it != everyone_cooldowns_.end();) {
    if (it->second < cutoff) {
      it = everyone_cooldowns_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<MentionManager::MentionSuggestion>
MentionManager::GetMentionSuggestions(const std::string& query,
                                     const std::string& channel_id,
                                     const std::string& user_id) {
  std::vector<MentionSuggestion> result;

  // In a real implementation, this would query:
  // - User service for user suggestions
  // - Social service for friend suggestions
  // - Channel manager for channel suggestions
  // - Group manager for role suggestions

  // For now, return placeholder suggestions
  std::string lower_query = query;
  std::transform(lower_query.begin(), lower_query.end(),
                 lower_query.begin(), ::tolower);

  if (query.empty() || lower_query.find("eve") != std::string::npos) {
    MentionSuggestion sugg;
    sugg.display_text = "@everyone";
    sugg.id = "";
    sugg.type = MentionType::EVERYONE;
    result.push_back(sugg);
  }

  if (query.empty() || lower_query.find("here") != std::string::npos) {
    MentionSuggestion sugg;
    sugg.display_text = "@here";
    sugg.id = "";
    sugg.type = MentionType::HERE;
    result.push_back(sugg);
  }

  return result;
}

std::vector<std::string> MentionManager::BuildNotificationRecipients(
    const ParsedMentions& parsed,
    const std::string& sender_id,
    const std::unordered_set<std::string>& channel_member_ids,
    const std::unordered_set<std::string>& online_user_ids) {

  std::vector<std::string> result;

  auto notify_ids = parsed.GetNotifyUserIds(sender_id, channel_member_ids);
  result.reserve(notify_ids.size());

  // For @here, only notify online users
  if (parsed.mentions_here && !parsed.mentions_everyone) {
    for (const auto& user_id : notify_ids) {
      if (online_user_ids.count(user_id) > 0) {
        result.push_back(user_id);
      }
    }
  } else {
    // For direct mentions and @everyone, notify all mentioned users
    result.insert(result.end(), notify_ids.begin(), notify_ids.end());
  }

  return result;
}

std::string MentionManager::FormatMentions(const std::string& content,
                                          const std::vector<Mention>& mentions) {
  if (mentions.empty()) {
    return content;
  }

  // Build formatted mentions (for UI display)
  std::string result = content;

  // Sort mentions by position (reverse order to avoid index shifting)
  std::vector<Mention> sorted = mentions;
  std::sort(sorted.begin(), sorted.end(),
    [](const Mention& a, const Mention& b) {
      return a.start_index() > b.start_index();
    });

  // Apply formatting
  for (const auto& mention : sorted) {
    size_t pos = static_cast<size_t>(mention.start_index());
    size_t len = static_cast<size_t>(mention.length());

    if (pos + len <= result.length()) {
      std::string replacement;

      switch (mention.type()) {
        case MentionType::USER:
          replacement = "**@" + result.substr(pos, len) + "**";
          break;
        case MentionType::ROLE:
          replacement = "**@" + result.substr(pos, len) + "**";
          break;
        case MentionType::CHANNEL:
          replacement = "**#" + result.substr(pos, len) + "**";
          break;
        case MentionType::EVERYONE:
          replacement = "**@everyone**";
          break;
        case MentionType::HERE:
          replacement = "**@here**";
          break;
        default:
          break;
      }

      result.replace(pos, len, replacement);
    }
  }

  return result;
}

} // namespace chat
} // namespace chirp
