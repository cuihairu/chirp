#ifndef CHIRP_CHAT_RECALL_TOMBSTONE_H_
#define CHIRP_CHAT_RECALL_TOMBSTONE_H_

#include <string>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace network {
class RedisClient;
} // namespace network

namespace chat {

// 撤回墓碑（game_chat_features P0 剩余缺口收口）：撤回/版主软删后，历史存档里
// 这条消息要同时「置位 is_recalled」并「抹除正文」——只置位的话 GET_HISTORY 仍
// 把原文端出去，撤回等于没做；墓碑的语义就是正文不复存在，读路径（历史、迁移、
// 归档）无需各自记得过滤。
//
// 两个形态的存档镜像同用 ChatMessage 的 proto 字节（基础形态 main.cc 的历史
// 向量/Redis 镜像、HybridMessageStore 的 Redis 热层），所以抹除逻辑收敛在这里
// 一份，三处 store 实现共用。

// 内存历史向量：原位改写命中条目（置位 + 清空正文），未命中不做任何事。
void MarkRecalledInMemory(std::vector<ChatMessage>& history,
                          const std::string& message_id);

// Redis 列表镜像（RPUSH 的 ChatMessage 字节）：LRange 扫过、命中条目置位并清空
// 正文后 LSet 原位回写——列表顺序不动，其余条目的读回不受影响。不可解析的条目
// 跳过（不是本工具写入的就不动它）。已标记的条目不重复回写（幂等，也免得白跑
// 一次 LSet）。返回所有实际发生的写是否成功。
bool MarkRecalledInRedisList(network::RedisClient& redis, const std::string& key,
                             const std::string& message_id);

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_RECALL_TOMBSTONE_H_
