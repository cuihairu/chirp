#include "recall_tombstone.h"

#include "network/redis_client.h"

namespace chirp {
namespace chat {

void MarkRecalledInMemory(std::vector<ChatMessage>& history,
                          const std::string& message_id) {
  for (auto& msg : history) {
    if (msg.message_id() == message_id && !msg.is_recalled()) {
      msg.set_is_recalled(true);
      msg.clear_content();
    }
  }
}

bool MarkRecalledInRedisList(network::RedisClient& redis, const std::string& key,
                             const std::string& message_id) {
  const auto raw = redis.LRange(key, 0, -1);
  bool ok = true;
  for (size_t i = 0; i < raw.size(); ++i) {
    ChatMessage msg;
    if (!msg.ParseFromArray(raw[i].data(), static_cast<int>(raw[i].size()))) {
      continue;
    }
    if (msg.message_id() != message_id || msg.is_recalled()) {
      continue;
    }
    msg.set_is_recalled(true);
    msg.clear_content();
    ok = redis.LSet(key, static_cast<int64_t>(i), msg.SerializeAsString()) && ok;
  }
  return ok;
}

} // namespace chat
} // namespace chirp
