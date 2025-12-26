#pragma once

#include "sdk.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <asio.hpp>

namespace chirp {
namespace sdk {

// 聊天客户端 SDK 类 (遵循 SOLID 原则)
class ChatClient {
public:
  explicit ChatClient(const ChatConfig& config);
  ~ChatClient();

  // 禁止拷贝和移动 (简单实现)
  ChatClient(const ChatClient&) = delete;
  ChatClient& operator=(const ChatClient&) = delete;
  ChatClient(ChatClient&&) = delete;
  ChatClient& operator=(ChatClient&&) = delete;

  // 连接管理
  void Connect();
  void Disconnect();
  ConnectionState GetState() const;

  // 认证
  void Login(const std::string& token, LoginCallback cb);
  void Logout();

  // 消息
  void SendMessage(const std::string& receiver, const std::string& content);
  void SetMessageCallback(MessageCallback cb);

  // 事件回调
  void SetDisconnectCallback(DisconnectCallback cb);
  void SetKickCallback(KickCallback cb);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace chirp
