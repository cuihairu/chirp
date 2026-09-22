#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace sdk {

// 消息存储接口：游戏实现此接口可自定义消息的本地缓存策略。
// 默认实现使用内存缓存；重度游戏可替换为 SQLite 等持久化方案。
class MessageStore {
 public:
  virtual ~MessageStore() = default;

  // 存储一条消息（收到的或发送的）。
  virtual void Save(const chirp::chat::ChatMessage& msg) = 0;

  // 加载历史消息。按时间倒序（最新在前）。
  // before_timestamp: 加载此时间之前的消息（0 = 从最新开始）。
  // limit: 最多返回条数。
  virtual std::vector<chirp::chat::ChatMessage> Load(
      chirp::chat::ChannelType type, const std::string& channel_id,
      int limit, int64_t before_timestamp = 0) = 0;

  // 标记消息已读。
  virtual void MarkRead(chirp::chat::ChannelType /*type*/,
                        const std::string& /*channel_id*/,
                        const std::string& /*message_id*/) {}

  // 获取未读消息数。
  virtual int GetUnreadCount(chirp::chat::ChannelType /*type*/,
                             const std::string& /*channel_id*/) {
    return 0;
  }

  // 清理过期消息。older_than: 清理此时间戳之前的消息。
  virtual void Cleanup(int64_t /*older_than*/) {}
};

// 内存消息存储（默认实现，轻量级游戏用）。
// Save 由 SDK io 线程调用，Load/MarkRead 等查询可从游戏线程直接调
// （ChatClient 的转发方法），内部互斥保护。
// 注意：不跟踪已读状态，GetUnreadCount 恒为 0；需要未读数的游戏自行实现
// MessageStore 并覆写 MarkRead/GetUnreadCount。
class MemoryMessageStore : public MessageStore {
 public:
  // max_per_channel: 每个频道保留的最大条数,超出淘汰最旧;0 表示不设上限。
  explicit MemoryMessageStore(size_t max_per_channel = 200);

  void Save(const chirp::chat::ChatMessage& msg) override;
  std::vector<chirp::chat::ChatMessage> Load(
      chirp::chat::ChannelType type, const std::string& channel_id,
      int limit, int64_t before_timestamp = 0) override;
  void Cleanup(int64_t older_than) override;

 private:
  size_t max_per_channel_;
  mutable std::mutex mutex_;
  // channel_key -> messages (按时间正序存储，Load 时倒序返回)
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>>
      store_;
};

}  // namespace sdk
}  // namespace chirp
