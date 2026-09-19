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

// 聊天客户端 SDK:直连 chat 网关的 TCP 长连接,[u32_be len][Packet protobuf]
// 帧。sequence 关联请求响应,25s 心跳(pong 回声校验,连续丢失判定死亡),
// 断线指数退避自动重连(KICK 终态),单请求超时。与 web/mobile/unity 的
// ChirpClient 同一套连接语义。
class ChatClient {
public:
  explicit ChatClient(const ChatConfig& config);
  ~ChatClient();

  // 禁止拷贝和移动
  ChatClient(const ChatClient&) = delete;
  ChatClient& operator=(const ChatClient&) = delete;
  ChatClient(ChatClient&&) = delete;
  ChatClient& operator=(ChatClient&&) = delete;

  // 连接管理。Connect() 可在 Kicked/Disconnected 后再次发起;运行中断线
  // 进入 WaitingReconnect 并自动重连,Disconnect() 取消一切重试。
  void Connect();
  void Disconnect();
  ConnectionState GetState() const;

  // 认证(LOGIN_REQ/LOGIN_RESP 的便捷封装)
  void Login(const std::string& token, LoginCallback cb);
  void Logout();

  // 消息:私聊文本便捷封装(fire-and-forget;要服务端 message id 用
  // Request(SendMessageReq/Resp))
  void SendMessage(const std::string& receiver, const std::string& content);
  void SetMessageCallback(MessageCallback cb);

  // 通用请求-响应:发送 msg_id_req,body 为请求消息字节;按 sequence 关联
  // msg_id_resp(不匹配的响应被忽略),超时回调 Timeout,断线/kick 回调
  // Closed/Kicked。可从任意线程调用。
  void Request(uint32_t msg_id_req, uint32_t msg_id_resp, const std::string& body,
               ResponseCallback cb);

  // 订阅 notify(sequence==0 的服务端推送),返回退订句柄。回调在 io 线程。
  NotifyHandle OnNotify(uint32_t msg_id, NotifyCallback cb);
  void OffNotify(NotifyHandle handle);

  // 事件回调
  void SetDisconnectCallback(DisconnectCallback cb);
  void SetKickCallback(KickCallback cb);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace chirp
