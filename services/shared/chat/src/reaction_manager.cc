#include "reaction_manager.h"

#include <algorithm>
#include <chrono>

namespace chirp {
namespace chat {

ReactionManager::ReactionManager() = default;

bool ReactionManager::AddReaction(const std::string& message_id,
                                 const std::string& user_id,
                                 const std::string& emoji,
                                 MessageReaction* out_reaction) {
  std::lock_guard<std::mutex> lock(mu_);

  // Get or create message reactions
  auto& msg_reactions = message_reactions_[message_id];
  if (!msg_reactions) {
    msg_reactions = std::make_shared<MessageReactions>();
    msg_reactions->message_id = message_id;
  }

  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  // Get or create reaction data for this emoji
  auto& reaction = msg_reactions->reactions[emoji];
  if (!reaction) {
    reaction = std::make_shared<ReactionData>();
    reaction->emoji = emoji;
    reaction->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
  }

  // Add user to reaction
  reaction->user_ids.insert(user_id);

  // Build output if requested
  if (out_reaction) {
    out_reaction->set_message_id(message_id);
    out_reaction->set_emoji(emoji);
    out_reaction->set_count(static_cast<int32_t>(reaction->user_ids.size()));

    // Include user IDs for small reactions (for UI preview)
    if (reaction->user_ids.size() <= 10) {
      for (const auto& uid : reaction->user_ids) {
        out_reaction->add_user_ids(uid);
      }
    }
  }

  return true;
}

bool ReactionManager::RemoveReaction(const std::string& message_id,
                                    const std::string& user_id,
                                    const std::string& emoji) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return false;  // Message has no reactions
  }

  auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  auto rit = msg_reactions->reactions.find(emoji);
  if (rit == msg_reactions->reactions.end()) {
    return false;  // No reactions with this emoji
  }

  auto& reaction = rit->second;
  reaction->user_ids.erase(user_id);

  // Clean up empty reactions
  if (reaction->user_ids.empty()) {
    msg_reactions->reactions.erase(rit);
  }

  // Clean up empty message reactions
  if (msg_reactions->reactions.empty()) {
    message_reactions_.erase(it);
  }

  return true;
}

std::vector<MessageReaction> ReactionManager::GetReactions(
    const std::string& message_id) {
  std::vector<MessageReaction> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return result;
  }

  const auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  result.reserve(msg_reactions->reactions.size());
  for (const auto& [emoji, reaction] : msg_reactions->reactions) {
    MessageReaction mr;
    mr.set_message_id(message_id);
    mr.set_emoji(reaction->emoji);
    mr.set_count(static_cast<int32_t>(reaction->user_ids.size()));

    // Include user IDs for small reactions
    if (reaction->user_ids.size() <= 10) {
      for (const auto& uid : reaction->user_ids) {
        mr.add_user_ids(uid);
      }
    }

    result.push_back(std::move(mr));
  }

  // Sort by count (most popular first)
  std::sort(result.begin(), result.end(),
    [](const MessageReaction& a, const MessageReaction& b) {
      return a.count() > b.count();
    });

  return result;
}

bool ReactionManager::GetReaction(const std::string& message_id,
                                 const std::string& emoji,
                                 MessageReaction* out_reaction) {
  if (!out_reaction) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return false;
  }

  const auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  auto rit = msg_reactions->reactions.find(emoji);
  if (rit == msg_reactions->reactions.end()) {
    return false;
  }

  const auto& reaction = rit->second;
  out_reaction->set_message_id(message_id);
  out_reaction->set_emoji(reaction->emoji);
  out_reaction->set_count(static_cast<int32_t>(reaction->user_ids.size()));

  if (reaction->user_ids.size() <= 10) {
    for (const auto& uid : reaction->user_ids) {
      out_reaction->add_user_ids(uid);
    }
  }

  return true;
}

bool ReactionManager::HasReacted(const std::string& message_id,
                                const std::string& user_id,
                                const std::string& emoji) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return false;
  }

  const auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  auto rit = msg_reactions->reactions.find(emoji);
  if (rit == msg_reactions->reactions.end()) {
    return false;
  }

  return rit->second->user_ids.count(user_id) > 0;
}

std::vector<std::string> ReactionManager::GetUserReactions(
    const std::string& message_id,
    const std::string& user_id) {
  std::vector<std::string> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return result;
  }

  const auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  for (const auto& [emoji, reaction] : msg_reactions->reactions) {
    if (reaction->user_ids.count(user_id) > 0) {
      result.push_back(emoji);
    }
  }

  return result;
}

void ReactionManager::ClearMessageReactions(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(mu_);
  message_reactions_.erase(message_id);
}

size_t ReactionManager::GetTotalReactionCount(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = message_reactions_.find(message_id);
  if (it == message_reactions_.end()) {
    return 0;
  }

  const auto& msg_reactions = it->second;
  std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

  size_t total = 0;
  for (const auto& [emoji, reaction] : msg_reactions->reactions) {
    total += reaction->user_ids.size();
  }

  return total;
}

std::vector<MessageReaction> ReactionManager::GetTopReactions(
    const std::string& message_id,
    size_t limit) {
  auto all = GetReactions(message_id);

  if (all.size() <= limit) {
    return all;
  }

  return std::vector<MessageReaction>(all.begin(), all.begin() + limit);
}

std::unordered_map<std::string, std::vector<MessageReaction>>
ReactionManager::GetReactionsForMessages(
    const std::vector<std::string>& message_ids) {
  std::unordered_map<std::string, std::vector<MessageReaction>> result;

  std::lock_guard<std::mutex> lock(mu_);

  for (const auto& message_id : message_ids) {
    auto it = message_reactions_.find(message_id);
    if (it == message_reactions_.end()) {
      result[message_id] = {};
      continue;
    }

    const auto& msg_reactions = it->second;
    std::lock_guard<std::mutex> msg_lock(msg_reactions->mu);

    std::vector<MessageReaction> reactions;
    reactions.reserve(msg_reactions->reactions.size());

    for (const auto& [emoji, reaction] : msg_reactions->reactions) {
      MessageReaction mr;
      mr.set_message_id(message_id);
      mr.set_emoji(reaction->emoji);
      mr.set_count(static_cast<int32_t>(reaction->user_ids.size()));

      if (reaction->user_ids.size() <= 10) {
        for (const auto& uid : reaction->user_ids) {
          mr.add_user_ids(uid);
        }
      }

      reactions.push_back(std::move(mr));
    }

    std::sort(reactions.begin(), reactions.end(),
      [](const MessageReaction& a, const MessageReaction& b) {
        return a.count() > b.count();
      });

    result[message_id] = std::move(reactions);
  }

  return result;
}

} // namespace chat
} // namespace chirp
