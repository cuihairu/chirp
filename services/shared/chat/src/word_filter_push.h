#pragma once

#include <memory>
#include <string>

#include "network/session.h"
#include "network/session_registry.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"
#include "word_filter.h"

namespace chirp::chat {

/// @brief 词库下发协议的共用装配面（basic/enhanced 两形态同一份，语义规约见
/// docs/design-notes/word_filter.md「词库下发协议」章节）。distributed 形态
/// 本就未接词库（无过滤），不接下发。

/// 把服务端当前词库状态填进 proto 载荷；known_version 命中时省略文本
/// （条件 GET，客户端按 version 相等跳过装载）。filter 为 null（未装配）
/// 时保持 proto 缺省：version 0 / enabled false / 空文本。
void FillWordFilterLexicon(const WordFilter* filter, int64_t known_version,
                           WordFilterLexicon* lexicon);

/// FETCH 分发（MsgID 2245 → 2246）：垃圾 body 回 INVALID_PARAM、未登录回
/// AUTH_FAILED（对齐 2239-2244 段守卫惯例），其余回当前词库。
void HandleWordFilterFetch(const gateway::Packet& pkt,
                           const std::shared_ptr<network::Session>& session,
                           const WordFilter* filter,
                           const std::string& authenticated_user_id);

/// 热更新广播（MsgID 2247）：向 registry 全部活会话推完整新词库——锁内
/// 快照、锁外投递（对齐设备清单广播）；死 weak_ptr 跳过。词库状态从
/// filter 现读。io 线程同步调用（set_on_reload 回调路径）。
void BroadcastWordFilterUpdate(const std::shared_ptr<network::SessionRegistry>& state,
                               const WordFilter& filter);

}  // namespace chirp::chat
