#pragma once

#include <cstdint>
#include <string>

#include "proto/chat.pb.h"

namespace chirp {
namespace sdk {

// 在线状态
enum class PresenceStatus {
  Online = 0,
  Busy = 1,
  Away = 2,
  Invisible = 3,
  Offline = 4,
};

// 事件监听器：SDK 在关键生命周期节点通知游戏。
// 游戏实现此接口可做：UI 更新、音效、统计、错误处理等。
class ChatEventListener {
 public:
  virtual ~ChatEventListener() = default;

  // 连接状态变化
  virtual void OnConnectionStateChanged(int /*ConnectionState*/ /*state*/) {}

  // 登录结果
  virtual void OnLoginResult(int /*code*/, const std::string& /*user_id*/) {}

  // 被踢下线（终态，需重新 Connect + Login）
  virtual void OnKicked(const std::string& /*reason*/) {}

  // 重连中（attempt 从 1 开始，delay_ms 是下次重试等待时间）
  virtual void OnReconnecting(int /*attempt*/, int /*delay_ms*/) {}

  // 重连成功
  virtual void OnReconnected() {}

  // 收到新消息（已通过 MessageInterceptor.OnBeforeReceive 过滤）
  virtual void OnMessageReceived(const chirp::chat::ChatMessage& /*msg*/) {}

  // 频道未读数变化
  virtual void OnUnreadChanged(chirp::chat::ChannelType /*type*/,
                               const std::string& /*channel_id*/,
                               int /*count*/) {}

  // 好友/公会成员在线状态变化
  virtual void OnPresenceChanged(const std::string& /*user_id*/,
                                 PresenceStatus /*status*/) {}

  // 正在输入指示
  virtual void OnTypingIndicator(const std::string& /*user_id*/,
                                 const std::string& /*channel_id*/,
                                 bool /*is_typing*/) {}

  // 走马灯消息（客户端应展示在屏幕顶部滚动）
  virtual void OnMarqueeMessage(const chirp::chat::ChatMessage& /*msg*/,
                                int /*ttl_seconds*/) {}

  // 系统公告（客户端应弹窗或高亮展示）
  virtual void OnSystemAnnouncement(const chirp::chat::ChatMessage& /*msg*/) {}
};

}  // namespace sdk
}  // namespace chirp
