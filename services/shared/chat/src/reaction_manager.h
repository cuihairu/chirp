#ifndef CHIRP_SERVICES_CHAT_REACTION_MANAGER_H_
#define CHIRP_SERVICES_CHAT_REACTION_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Per-emoji reaction data
struct ReactionData {
  std::string emoji;
  std::unordered_set<std::string> user_ids;  // Users who reacted
  int64_t created_at = 0;
  mutable std::mutex mu;
};

// Per-message reactions
struct MessageReactions {
  std::string message_id;
  std::unordered_map<std::string, std::shared_ptr<ReactionData>> reactions;
  mutable std::mutex mu;
};

// Manages emoji reactions on messages
class ReactionManager {
public:
  ReactionManager();
  ~ReactionManager() = default;

  // Add reaction to message
  bool AddReaction(const std::string& message_id,
                  const std::string& user_id,
                  const std::string& emoji,
                  MessageReaction* out_reaction = nullptr);

  // Remove reaction from message
  bool RemoveReaction(const std::string& message_id,
                     const std::string& user_id,
                     const std::string& emoji);

  // Get all reactions for a message
  std::vector<MessageReaction> GetReactions(const std::string& message_id);

  // Get reactions for a specific emoji
  bool GetReaction(const std::string& message_id,
                  const std::string& emoji,
                  MessageReaction* out_reaction);

  // Check if user reacted with specific emoji
  bool HasReacted(const std::string& message_id,
                 const std::string& user_id,
                 const std::string& emoji);

  // Get all reactions by user in a message
  std::vector<std::string> GetUserReactions(const std::string& message_id,
                                            const std::string& user_id);

  // Clear all reactions from a message (used when message is deleted)
  void ClearMessageReactions(const std::string& message_id);

  // Get reaction count for a message
  size_t GetTotalReactionCount(const std::string& message_id);

  // Get top reactions (for UI display)
  std::vector<MessageReaction> GetTopReactions(const std::string& message_id,
                                               size_t limit = 10);

  // Bulk operations
  std::unordered_map<std::string, std::vector<MessageReaction>>
    GetReactionsForMessages(const std::vector<std::string>& message_ids);

private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<MessageReactions>> message_reactions_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_REACTION_MANAGER_H_
