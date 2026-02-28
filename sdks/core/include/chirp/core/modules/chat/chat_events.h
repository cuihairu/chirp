#ifndef CHIRP_CORE_MODULES_CHAT_CHAT_EVENTS_H_
#define CHIRP_CORE_MODULES_CHAT_CHAT_EVENTS_H_

#include <functional>
#include <string>
#include <vector>

#include "chirp/core/modules/chat/chat_module.h"

namespace chirp {
namespace core {
namespace modules {
namespace chat {

// Group member info
struct GroupMember {
  std::string user_id;
  std::string username;
  std::string avatar_url;
  int32_t role;
  int64_t joined_at;
  int64_t last_read_at;
};

// Message received event callback
using MessageCallback = std::function<void(const Message& message)>;

// Message read receipt callback
using MessageReadCallback = std::function<void(const std::string& channel_id,
                                              ChannelType channel_type,
                                              const std::string& message_id,
                                              const std::string& reader_user_id,
                                              int64_t read_at)>;

// Typing indicator callback
using TypingCallback = std::function<void(const std::string& channel_id,
                                         ChannelType channel_type,
                                         const std::string& user_id,
                                         bool is_typing)>;

// Group event callbacks
using GroupCreatedCallback = std::function<void(const std::string& group_id,
                                               const std::string& name)>;

using GroupMemberJoinedCallback = std::function<void(const std::string& group_id,
                                                    const GroupMember& member)>;

using GroupMemberLeftCallback = std::function<void(const std::string& group_id,
                                                   const std::string& user_id)>;

using GroupMemberKickedCallback = std::function<void(const std::string& group_id,
                                                    const std::string& user_id,
                                                    const std::string& kicked_by)>;

} // namespace chat
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_CHAT_CHAT_EVENTS_H_
