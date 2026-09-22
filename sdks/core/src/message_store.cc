#include "chirp/message_store.h"

#include <algorithm>

namespace chirp {
namespace sdk {

namespace {

// 同一 channel_id 在不同 ChannelType 下互不干扰,组合成扁平 key。
std::string StoreKey(chirp::chat::ChannelType type, const std::string& channel_id) {
  return std::to_string(static_cast<int>(type)) + "|" + channel_id;
}

}  // namespace

MemoryMessageStore::MemoryMessageStore(size_t max_per_channel)
    : max_per_channel_(max_per_channel) {}

void MemoryMessageStore::Save(const chirp::chat::ChatMessage& msg) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bucket = store_[StoreKey(msg.channel_type(), msg.channel_id())];
  bucket.push_back(msg);
  // max_per_channel_ == 0 表示不设上限;超出时淘汰最旧,桶内保持时间正序。
  if (max_per_channel_ > 0 && bucket.size() > max_per_channel_) {
    bucket.erase(bucket.begin());
  }
}

std::vector<chirp::chat::ChatMessage> MemoryMessageStore::Load(
    chirp::chat::ChannelType type, const std::string& channel_id,
    int limit, int64_t before_timestamp) {
  std::vector<chirp::chat::ChatMessage> result;
  if (limit <= 0) {
    return result;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = store_.find(StoreKey(type, channel_id));
  if (it == store_.end()) {
    return result;
  }
  result.reserve(static_cast<size_t>(limit));
  // 桶内按时间正序存放;从尾向前收集至多 limit 条,结果天然最新在前。
  for (auto rit = it->second.rbegin();
       rit != it->second.rend() && static_cast<int>(result.size()) < limit;
       ++rit) {
    if (before_timestamp != 0 && rit->timestamp() >= before_timestamp) {
      continue;
    }
    result.push_back(*rit);
  }
  return result;
}

void MemoryMessageStore::Cleanup(int64_t older_than) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = store_.begin(); it != store_.end();) {
    auto& bucket = it->second;
    bucket.erase(
        std::remove_if(bucket.begin(), bucket.end(),
                       [older_than](const chirp::chat::ChatMessage& msg) {
                         return msg.timestamp() < older_than;
                       }),
        bucket.end());
    if (bucket.empty()) {
      it = store_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace sdk
}  // namespace chirp
