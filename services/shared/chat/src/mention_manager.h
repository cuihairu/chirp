#ifndef CHIRP_SERVICES_CHAT_MENTION_MANAGER_H_
#define CHIRP_SERVICES_CHAT_MENTION_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Mention parser result
struct ParsedMentions {
  std::vector<Mention> mentions;
  std::unordered_set<std::string> mentioned_user_ids;
  bool mentions_everyone = false;
  bool mentions_here = false;

  // For notification building
  std::unordered_set<std::string> GetNotifyUserIds(
      const std::string& sender_id,
      const std::unordered_set<std::string>& channel_member_ids) const;
};

// Configuration for mentions
struct MentionConfig {
  int32_t max_mentions_per_message = 20;  // Prevent abuse
  bool allow_everyone = true;             // @everyone
  bool allow_here = true;                 // @here
  int64_t everyone_cooldown_ms = 60000;   // 1 minute between @everyone
};

// Manages message mentions and notifications
class MentionManager {
public:
  explicit MentionManager(const MentionConfig& config = MentionConfig());
  ~MentionManager() = default;

  // Parse message content for mentions
  ParsedMentions ParseMentions(const std::string& content,
                              const std::string& sender_id);

  // Validate mentions (permission check, cooldown check)
  bool CanMentionEveryone(const std::string& user_id,
                         const std::string& channel_id,
                         bool is_moderator = false);

  bool CanMentionHere(const std::string& user_id,
                     const std::string& channel_id,
                     bool is_moderator = false);

  // Record @everyone usage for cooldown
  void RecordEveryoneMention(const std::string& user_id,
                            const std::string& channel_id);

  // Get mention suggestions (autocomplete)
  struct MentionSuggestion {
    std::string display_text;   // e.g., "@username"
    std::string id;             // User ID or Role ID
    MentionType type;
    std::string icon_url;       // For UI
  };

  std::vector<MentionSuggestion> GetMentionSuggestions(
      const std::string& query,
      const std::string& channel_id,
      const std::string& user_id);

  // Build notification recipients
  std::vector<std::string> BuildNotificationRecipients(
      const ParsedMentions& parsed,
      const std::string& sender_id,
      const std::unordered_set<std::string>& channel_member_ids,
      const std::unordered_set<std::string>& online_user_ids);

  // Format message with mentions (for display)
  std::string FormatMentions(const std::string& content,
                            const std::vector<Mention>& mentions);

private:
  // Last @everyone usage per user per channel
  std::unordered_map<std::string, int64_t> everyone_cooldowns_;

  int64_t GetCurrentTimeMs() const;

  MentionConfig config_;
  mutable std::mutex mu_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_MENTION_MANAGER_H_
