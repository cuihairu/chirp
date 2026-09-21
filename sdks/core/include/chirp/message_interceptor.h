#pragma once

#include <cstdint>
#include <string>

#include "proto/chat.pb.h"

namespace chirp {
namespace sdk {

// 消息拦截器：在消息发送前/接收后插入自定义逻辑。
// 游戏实现此接口可做：敏感词过滤、消息格式化、VIP 特效、音效触发等。
// 所有回调在 SDK 内部 io 线程触发，引擎适配层负责派发回游戏线程。
class MessageInterceptor {
 public:
  virtual ~MessageInterceptor() = default;

  // 发送前拦截。可修改 msg 内容/添加 metadata。
  // 返回 false 表示阻止发送（消息不会到达服务器）。
  virtual bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) {
    return true;
  }

  // 发送后回调。消息已到达服务器（或已进入发送队列）。
  // 用于统计、日志、发送特效。
  virtual void OnAfterSend(const chirp::chat::SendMessageRequest& msg) {}

  // 接收前拦截。可修改 msg 内容或丢弃消息。
  // 返回 false 表示丢弃（不触发 OnAfterReceive，不展示给玩家）。
  virtual bool OnBeforeReceive(chirp::chat::ChatMessage& msg) { return true; }

  // 接收后回调。消息即将展示给玩家。
  // 用于音效、振动、桌面通知、未读计数。
  virtual void OnAfterReceive(const chirp::chat::ChatMessage& msg) {}
};

}  // namespace sdk
}  // namespace chirp
