#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "message_store.h"
#include "proto/chat.pb.h"

namespace chirp {
namespace sdk {

// 文件持久化消息存储(header-only,与 C# SDK 的 FileMessageStore 同一文件
// 格式):append-only 日志,文件头 `CHIRPLOG1` + 记录序列
// `[1B kind][4B len(LE)][payload]`;kind 0x01 = 消息(ChatMessage protobuf
// 字节),0x02 = 已读标记("`channelKey\nmessageId`" UTF-8 文本)。
//
// 语义与 C# 版逐条对齐:
// - 启动重放建立内存索引;坏头(空文件/他源文件)按损坏处理,重置为新档;
//   坏记录只保留之前重放成功的部分,不抛异常。
// - 超限淘汰/Cleanup 只动内存索引,不重写文件——因此未 Compact 的频道
//   重启后可能复活被淘汰的旧条目;需要紧收历史时调 Compact()
//   (tmp + 原子替换,POSIX;重写成本 O(存量),勿在高频路径调用)。
//   要淘汰条目真正落定,Compact 必须由"带上限的实例"执行:重放时旧条目
//   在内存即被淘汰,Compact 快照才不含它。
// - 残尾(崩溃时写了一半)在重放后就地截掉,后续追加永远接在干净边界上。
// - Save 由 SDK io 线程调用,Load/MarkRead 等查询可从游戏线程直接调,
//   内部互斥保护。
class FileMessageStore : public MessageStore {
 public:
  struct Options {
    std::string path;  // 存档文件路径;不存在则创建,存在则重放。
    size_t max_per_channel = 200;  // 每频道内存上限,超出淘汰最旧;0 = 不设限。
  };

  explicit FileMessageStore(Options opts);

  void Save(const chirp::chat::ChatMessage& msg) override;
  std::vector<chirp::chat::ChatMessage> Load(
      chirp::chat::ChannelType type, const std::string& channel_id, int limit,
      int64_t before_timestamp = 0) override;
  void MarkRead(chirp::chat::ChannelType type, const std::string& channel_id,
                const std::string& message_id) override;
  int GetUnreadCount(chirp::chat::ChannelType type,
                     const std::string& channel_id) override;
  void Cleanup(int64_t older_than) override;

  /// 把当前内存快照原子重写回文件:此后被淘汰/清理的条目不再随重放复活。
  void Compact();

 private:
  using Bucket = std::vector<chirp::chat::ChatMessage>;

  static std::string Key(chirp::chat::ChannelType type,
                         const std::string& channel_id) {
    return std::to_string(static_cast<int>(type)) + "|" + channel_id;
  }

  void Replay();
  void AddToBucketLocked(const chirp::chat::ChatMessage& msg);
  void AppendRecordLocked(uint8_t kind, const std::string& payload);
  void WriteHeaderLocked();
  void TruncateTo(std::uintmax_t length);
  void CompactLocked();

  std::string path_;
  size_t max_per_channel_;
  mutable std::mutex mutex_;
  // channel_key -> messages(按时间正序存储,Load 时倒序返回)
  std::unordered_map<std::string, Bucket> channels_;
  // channel_key -> 已读 message_id 集合
  std::unordered_map<std::string, std::set<std::string>> reads_;
};

namespace file_store_detail {

constexpr char kMagic[] = "CHIRPLOG1";
constexpr uint8_t kKindMessage = 0x01;
constexpr uint8_t kKindRead = 0x02;

inline void PutUint32Le(std::string& out, uint32_t value) {
  out.push_back(static_cast<char>(value & 0xFFu));
  out.push_back(static_cast<char>((value >> 8) & 0xFFu));
  out.push_back(static_cast<char>((value >> 16) & 0xFFu));
  out.push_back(static_cast<char>((value >> 24) & 0xFFu));
}

inline uint32_t GetUint32Le(const std::string& bytes, size_t offset) {
  return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24);
}

}  // namespace file_store_detail

inline FileMessageStore::FileMessageStore(Options opts)
    : path_(std::move(opts.path)), max_per_channel_(opts.max_per_channel) {
  Replay();
}

inline void FileMessageStore::Save(const chirp::chat::ChatMessage& msg) {
  std::lock_guard<std::mutex> lock(mutex_);
  AppendRecordLocked(file_store_detail::kKindMessage, msg.SerializeAsString());
  AddToBucketLocked(msg);
}

inline std::vector<chirp::chat::ChatMessage> FileMessageStore::Load(
    chirp::chat::ChannelType type, const std::string& channel_id, int limit,
    int64_t before_timestamp) {
  std::vector<chirp::chat::ChatMessage> result;
  if (limit <= 0) return result;
  const auto key = Key(type, channel_id);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = channels_.find(key);
  if (it == channels_.end()) return result;
  for (auto i = it->second.size();
       i-- > 0 && result.size() < static_cast<size_t>(limit);) {
    const auto& message = it->second[i];
    if (before_timestamp == 0 || message.timestamp() < before_timestamp) {
      result.push_back(message);
    }
  }
  return result;
}

inline void FileMessageStore::MarkRead(chirp::chat::ChannelType type,
                                       const std::string& channel_id,
                                       const std::string& message_id) {
  if (message_id.empty()) return;
  const auto key = Key(type, channel_id);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!reads_[key].insert(message_id).second) return;  // 已读:保持日志精简
  AppendRecordLocked(file_store_detail::kKindRead, key + "\n" + message_id);
}

inline int FileMessageStore::GetUnreadCount(chirp::chat::ChannelType type,
                                            const std::string& channel_id) {
  const auto key = Key(type, channel_id);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto bucket = channels_.find(key);
  if (bucket == channels_.end()) return 0;
  const auto ids = reads_.find(key);
  int unread = 0;
  for (const auto& message : bucket->second) {
    // 发送侧本地存档的 message_id 为空,天然不计未读。
    if (!message.message_id().empty() &&
        (ids == reads_.end() || ids->second.find(message.message_id()) ==
                                    ids->second.end())) {
      ++unread;
    }
  }
  return unread;
}

inline void FileMessageStore::Cleanup(int64_t older_than) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool removed = false;
  for (auto& [key, bucket] : channels_) {
    const auto keep =
        std::remove_if(bucket.begin(), bucket.end(), [&](const auto& m) {
          return m.timestamp() < older_than;
        });
    if (keep != bucket.end()) {
      bucket.erase(keep, bucket.end());
      removed = true;
    }
  }
  if (removed) CompactLocked();
}

inline void FileMessageStore::Compact() {
  std::lock_guard<std::mutex> lock(mutex_);
  CompactLocked();
}

// ---- internals -----------------------------------------------------------

inline void FileMessageStore::Replay() {
  namespace fs = std::filesystem;
  std::string blob;
  try {
    if (fs::exists(path_)) {
      std::ifstream in(path_, std::ios::binary);
      blob.assign(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>());
    }
  } catch (const std::exception&) {
    blob.clear();
  }

  namespace d = file_store_detail;
  const size_t magic_len = sizeof(d::kMagic) - 1;
  size_t offset = 0;
  if (blob.size() >= magic_len && blob.compare(0, magic_len, d::kMagic) == 0) {
    offset = magic_len;
    try {
      while (offset + 5 <= blob.size()) {
        const auto kind = static_cast<uint8_t>(blob[offset]);
        const uint32_t length = d::GetUint32Le(blob, offset + 1);
        if (kind != d::kKindMessage && kind != d::kKindRead) break;
        if (offset + 5 + length > blob.size()) break;  // truncated tail
        const std::string payload = blob.substr(offset + 5, length);
        offset += 5 + length;
        if (kind == d::kKindMessage) {
          chirp::chat::ChatMessage message;
          if (!message.ParseFromString(payload)) break;
          AddToBucketLocked(message);
        } else {
          const auto nl = payload.find('\n');
          if (nl == std::string::npos || nl == 0) continue;
          reads_[payload.substr(0, nl)].insert(payload.substr(nl + 1));
        }
      }
    } catch (const std::exception&) {
      // Corrupt record: keep everything replayed before it.
    }

    // 残尾就地截掉,保证后续 append 永远接在干净边界上;完整记录一字节不动。
    if (offset < blob.size()) TruncateTo(offset);
  }

  // 坏头/空文件:铺一个新头;已识别的日志:只补缺失的头。
  if (offset < magic_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    WriteHeaderLocked();
  }
}

inline void FileMessageStore::AddToBucketLocked(
    const chirp::chat::ChatMessage& msg) {
  const auto key = Key(msg.channel_type(), msg.channel_id());
  auto& bucket = channels_[key];
  bucket.push_back(msg);
  if (max_per_channel_ > 0 && bucket.size() > max_per_channel_) {
    bucket.erase(bucket.begin());  // in-memory only; Compact() retires it on disk
  }
}

inline void FileMessageStore::AppendRecordLocked(uint8_t kind,
                                                 const std::string& payload) {
  std::string record;
  record.reserve(5 + payload.size());
  record.push_back(static_cast<char>(kind));
  file_store_detail::PutUint32Le(record,
                                 static_cast<uint32_t>(payload.size()));
  record.append(payload);
  std::ofstream out(path_, std::ios::app | std::ios::binary);
  out.write(record.data(), static_cast<std::streamsize>(record.size()));
}

inline void FileMessageStore::WriteHeaderLocked() {
  const auto parent = std::filesystem::path(path_).parent_path();
  std::error_code ec;
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  out.write(file_store_detail::kMagic,
            static_cast<std::streamsize>(sizeof(file_store_detail::kMagic) - 1));
}

inline void FileMessageStore::TruncateTo(std::uintmax_t length) {
  std::error_code ec;
  std::filesystem::resize_file(path_, length, ec);
  // 截不掉也不影响本次会话:内存索引已是干净前缀,残尾会在下次重放时
  // 再次尝试移除。
}

inline void FileMessageStore::CompactLocked() {
  namespace d = file_store_detail;
  // 已删消息的已读标记没有存在的意义:先丢弃,再落盘快照,保证 Compact
  // 之后文件里不再有死条目。
  for (auto it = reads_.begin(); it != reads_.end();) {
    if (channels_.find(it->first) == channels_.end()) {
      it = reads_.erase(it);
    } else {
      std::set<std::string> live;
      for (const auto& message : channels_[it->first]) {
        if (!message.message_id().empty()) live.insert(message.message_id());
      }
      std::set<std::string> kept;
      for (const auto& id : it->second) {
        if (live.count(id) > 0) kept.insert(id);
      }
      it->second = std::move(kept);
      ++it;
    }
  }

  // 先写临时文件再原子替换:重写中途崩溃也不会损坏旧档案(POSIX rename
  // 原子覆盖;Windows 目标接入时按平台补 MoveFileEx 的 REPLACE_EXISTING)。
  const std::string tmp = path_ + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(d::kMagic, static_cast<std::streamsize>(sizeof(d::kMagic) - 1));
    std::string record;
    for (const auto& [key, bucket] : channels_) {
      for (const auto& message : bucket) {
        record.clear();
        record.push_back(static_cast<char>(d::kKindMessage));
        const std::string bytes = message.SerializeAsString();
        d::PutUint32Le(record, static_cast<uint32_t>(bytes.size()));
        record.append(bytes);
        out.write(record.data(), static_cast<std::streamsize>(record.size()));
      }
    }
    for (const auto& [key, ids] : reads_) {
      for (const auto& id : ids) {
        record.clear();
        record.push_back(static_cast<char>(d::kKindRead));
        const std::string bytes = key + "\n" + id;
        d::PutUint32Le(record, static_cast<uint32_t>(bytes.size()));
        record.append(bytes);
        out.write(record.data(), static_cast<std::streamsize>(record.size()));
      }
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path_, ec);
}

}  // namespace sdk
}  // namespace chirp
