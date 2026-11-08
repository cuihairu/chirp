#ifndef CHIRP_CHAT_BASIC_MESSAGE_STORE_H_
#define CHIRP_CHAT_BASIC_MESSAGE_STORE_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "delivery_ack_manager.h"
#include "network/redis_client.h"
#include "proto/chat.pb.h"
#include "recall_tombstone.h"
#include "runtime_utils.h"

namespace chirp::chat::basic {

struct MessageStore {
  static constexpr size_t kMaxHistory = 100;
  static constexpr size_t kMaxOfflineInMemory = 200;

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};

  // channel_key -> messages
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> history;
  // receiver_id -> pending messages (redis 不可用时兜底)
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> offline_messages;

  explicit MessageStore(std::shared_ptr<chirp::network::RedisClient> redis_client, int ttl)
      : redis(std::move(redis_client)), offline_ttl_seconds(ttl) {}

  std::string HistoryKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return "chat:history:" + ChannelKey(type, channel_id);
  }

  std::string ChannelKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return std::to_string(static_cast<int>(type)) + ":" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) {
    if (a <= b) {
      return a + "|" + b;
    }
    return b + "|" + a;
  }

  // 消息引用（game_chat_features P1）：回复目标必须能在同一会话的历史里找到，
  // 否则引用在客户端渲染不出摘要。历史里始终有本进程写入的全部消息（Redis
  // 只是镜像），所以内存扫描即完整判据。
  bool HasMessage(chirp::chat::ChannelType type, const std::string& channel_id,
                  const std::string& message_id) {
    const auto it = history.find(ChannelKey(type, channel_id));
    if (it == history.end()) {
      return false;
    }
    for (const auto& msg : it->second) {
      if (msg.message_id() == message_id) {
        return true;
      }
    }
    return false;
  }

  void AddMessage(const chirp::chat::ChatMessage& msg) {
    if (redis) {
      redis->RPush(HistoryKey(msg.channel_type(), msg.channel_id()), msg.SerializeAsString());
    }

    auto& msgs = history[ChannelKey(msg.channel_type(), msg.channel_id())];
    msgs.push_back(msg);

    // 只保留最近 100 条消息
    if (msgs.size() > kMaxHistory) {
      msgs.erase(msgs.begin());
    }
  }

  // 离线队列 per-device 拆分(P1-5 余项第二块):桶 slot = 规范化 platform,
  // 与 SessionRegistry 的 (user, platform) 槽位同单位。default 桶保持拆分前
  // 的键形(零迁移):发送时接收方无任何在线端 → 目标端未知,副本落共享桶,
  // 任何端的登录先到先得;命名设备桶只装「明确为某端而存在」的副本(登录
  // 补投认领后未 ack 的回队重投)。Redis 侧不维护设备花名册(那是 Presence
  // 邻接面,明确不做)。
  std::string OfflineKey(const std::string& user_id, const std::string& slot) {
    if (slot == chirp::chat::kDefaultOfflineSlot) {
      return "chat:offline:" + user_id;  // 遗留键形 = default 桶
    }
    return "chat:offline:" + user_id + ":" + slot;
  }

  void AddOffline(const std::string& receiver_id, const std::string& slot,
                  const chirp::chat::ChatMessage& msg) {
    if (receiver_id.empty()) {
      return;
    }
    if (redis && redis->RPush(OfflineKey(receiver_id, slot), msg.SerializeAsString())) {
      if (offline_ttl_seconds > 0) {
        redis->Expire(OfflineKey(receiver_id, slot), offline_ttl_seconds);
      }
      return;
    }

    auto& pending = offline_messages[OfflineKey(receiver_id, slot)];
    pending.push_back(msg);
    if (pending.size() > kMaxOfflineInMemory) {
      pending.erase(pending.begin());
    }
  }

  // 补投臂:弹登录端自己的桶 ∪ default 共享桶(本槽优先)。「无主」副本
  // (入队臂落 default)任何端登录都清,先到先得;其他设备桶里的回队重投
  // 副本只有对应端登录才领,不被本端截走。
  std::vector<chirp::chat::ChatMessage> PopOffline(const std::string& user_id,
                                                   const std::string& slot) {
    std::vector<chirp::chat::ChatMessage> out;
    if (user_id.empty()) {
      return out;
    }

    out = DrainBucket(user_id, slot);
    if (slot != chirp::chat::kDefaultOfflineSlot) {
      auto shared = DrainBucket(user_id, chirp::chat::kDefaultOfflineSlot);
      out.insert(out.end(), std::make_move_iterator(shared.begin()),
                 std::make_move_iterator(shared.end()));
    }
    return out;
  }

 private:
  // 弹空一只桶:先 Redis(LRange + Del),空了再翻内存兜底。map 键与 Redis
  // 键同串,两种桶一个口径。
  std::vector<chirp::chat::ChatMessage> DrainBucket(const std::string& user_id,
                                                    const std::string& slot) {
    std::vector<chirp::chat::ChatMessage> out;
    const std::string key = OfflineKey(user_id, slot);

    if (redis) {
      auto raw = redis->LRange(key, 0, -1);
      if (!raw.empty()) {
        out.reserve(raw.size());
        for (const auto& item : raw) {
          chirp::chat::ChatMessage m;
          if (m.ParseFromArray(item.data(), static_cast<int>(item.size()))) {
            out.push_back(std::move(m));
          }
        }
      }
      redis->Del(key);
      if (!out.empty()) {
        return out;
      }
    }

    auto it = offline_messages.find(key);
    if (it == offline_messages.end()) {
      return out;
    }
    out = std::move(it->second);
    offline_messages.erase(it);
    return out;
  }

 public:
  // Delivery-ack requeue path: pushes the exact bytes that were tracked so a
  // late ack can remove the copy byte-for-byte (no parse/re-serialize).
  // slot 来自 Track 记账:在线首投回 default 共享桶,补投回队回认领端桶。
  void AddOfflineBytes(const std::string& receiver_id, const std::string& slot,
                       const std::string& bytes) {
    if (receiver_id.empty()) {
      return;
    }
    if (redis && redis->RPush(OfflineKey(receiver_id, slot), bytes)) {
      if (offline_ttl_seconds > 0) {
        redis->Expire(OfflineKey(receiver_id, slot), offline_ttl_seconds);
      }
      return;
    }
    chirp::chat::ChatMessage msg;
    if (msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
      auto& pending = offline_messages[OfflineKey(receiver_id, slot)];
      pending.push_back(std::move(msg));
      if (pending.size() > kMaxOfflineInMemory) {
        pending.erase(pending.begin());
      }
    }
  }

  // Late-ack cleanup: drop the offline copy the client confirmed after it had
  // already been requeued. The Redis entry matches byte-for-byte; the
  // in-memory fallback matches by message_id. slot 精确定位回队时的桶。
  bool RemoveOffline(const std::string& receiver_id, const std::string& slot,
                     const std::string& bytes) {
    if (receiver_id.empty()) {
      return false;
    }
    bool removed = false;
    if (redis) {
      removed = redis->LRem(OfflineKey(receiver_id, slot), 1, bytes) > 0;
    }
    chirp::chat::ChatMessage msg;
    if (msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
      auto it = offline_messages.find(OfflineKey(receiver_id, slot));
      if (it != offline_messages.end()) {
        auto& pending = it->second;
        for (auto mit = pending.begin(); mit != pending.end(); ++mit) {
          if (mit->message_id() == msg.message_id()) {
            pending.erase(mit);
            removed = true;
            break;
          }
        }
      }
    }
    return removed;
  }

  // 撤回/版主删除后回收离线队列里的副本(game_chat_features P0 消息撤回):
  // 离线接收方没有 live 会话,收不到 MESSAGE_DELETED_NOTIFY,若副本仍在队列里
  // 就会在下次登录时把原文补投出去,撤回等于白做。Redis 侧没有 message_id 索引,
  // 只能扫队列按 message_id 匹配后 LRem(队列有每用户 200 条内存/TTL 上限,代价有界)。
  // per-device 拆分后副本可能落在任意设备桶:扫 default 桶 + KEYS 前缀全设备桶
  // + 内存兜底同前缀。message_id 全局唯一,跨桶误删不存在。
  size_t PurgeOfflineByMessageId(const std::string& receiver_id,
                                 const std::string& message_id) {
    if (receiver_id.empty() || message_id.empty()) {
      return 0;
    }
    size_t removed = 0;

    // 内存兜底:default 桶键精确,设备桶键 = default 键 + ":" 前缀
    // (前缀刻意带分隔符,不与相邻用户名相撞)。
    const std::string default_key = OfflineKey(receiver_id, chirp::chat::kDefaultOfflineSlot);
    const std::string slot_prefix = default_key + ":";
    for (auto it = offline_messages.begin(); it != offline_messages.end();) {
      if (it->first == default_key || it->first.compare(0, slot_prefix.size(), slot_prefix) == 0) {
        auto& pending = it->second;
        const size_t before = pending.size();
        pending.erase(std::remove_if(pending.begin(), pending.end(),
                                     [&message_id](const chirp::chat::ChatMessage& queued) {
                                       return queued.message_id() == message_id;
                                     }),
                      pending.end());
        removed += before - pending.size();
        it = pending.empty() ? offline_messages.erase(it) : std::next(it);
      } else {
        ++it;
      }
    }

    if (redis) {
      std::vector<std::string> keys{default_key};
      for (const auto& key : redis->Keys(slot_prefix + "*")) {
        keys.push_back(key);
      }
      for (const auto& key : keys) {
        for (const auto& item : redis->LRange(key, 0, -1)) {
          chirp::chat::ChatMessage queued;
          if (queued.ParseFromArray(item.data(), static_cast<int>(item.size())) &&
              queued.message_id() == message_id && redis->LRem(key, 1, item) > 0) {
            ++removed;
          }
        }
      }
    }
    return removed;
  }

  // 撤回墓碑（game_chat_features P0）：撤回/版主软删后把历史存档里的这条
  // 消息置为已撤回并抹除正文——墓碑的语义是原文不复存在，只置位的话
  // GET_HISTORY 仍会把原文端出去。内存向量原位改写，Redis 镜像按 LSet 原位
  // 回写（列表顺序不动）。收敛在 recall_tombstone.h，与 Hybrid 热层同一份逻辑。
  bool MarkRecalled(chirp::chat::ChannelType type, const std::string& channel_id,
                    const std::string& message_id) {
    bool ok = true;

    auto it = history.find(ChannelKey(type, channel_id));
    if (it != history.end()) {
      chirp::chat::MarkRecalledInMemory(it->second, message_id);
    }

    if (redis) {
      ok = chirp::chat::MarkRecalledInRedisList(*redis, HistoryKey(type, channel_id),
                                                message_id) && ok;
    }
    return ok;
  }

  std::vector<chirp::chat::ChatMessage> GetHistory(chirp::chat::ChannelType type,
                                                   const std::string& channel_id,
                                                   int64_t before_timestamp,
                                                   int32_t limit,
                                                   bool* has_more) {
    if (has_more) {
      *has_more = false;
    }

    int64_t before = before_timestamp;
    if (before <= 0) {
      before = chirp::chat::runtime::NowMs() + 1;
    }
    int32_t lim = limit;
    if (lim <= 0) {
      lim = 50;
    }

    if (redis) {
      auto raw = redis->LRange(HistoryKey(type, channel_id), 0, -1);
      if (!raw.empty()) {
        std::vector<chirp::chat::ChatMessage> result;
        result.reserve(raw.size());
        for (auto rit = raw.rbegin(); rit != raw.rend(); ++rit) {
          chirp::chat::ChatMessage msg;
          if (!msg.ParseFromArray(rit->data(), static_cast<int>(rit->size()))) {
            continue;
          }
          if (msg.timestamp() >= before) {
            continue;
          }
          result.push_back(std::move(msg));
          if (static_cast<int32_t>(result.size()) >= lim) {
            if (has_more) {
              *has_more = (rit + 1) != raw.rend();
            }
            break;
          }
        }
        std::reverse(result.begin(), result.end());
        return result;
      }
    }

    auto it = history.find(ChannelKey(type, channel_id));
    if (it == history.end()) {
      return {};
    }

    const auto& all_msgs = it->second;
    std::vector<chirp::chat::ChatMessage> result;
    for (auto rit = all_msgs.rbegin(); rit != all_msgs.rend(); ++rit) {
      if (rit->timestamp() >= before) {
        continue;
      }
      result.push_back(*rit);
      if (static_cast<int32_t>(result.size()) >= lim) {
        if (has_more) {
          *has_more = (rit + 1) != all_msgs.rend();
        }
        break;
      }
    }

    std::reverse(result.begin(), result.end());
    return result;
  }
};

}  // namespace chirp::chat::basic

#endif  // CHIRP_CHAT_BASIC_MESSAGE_STORE_H_
