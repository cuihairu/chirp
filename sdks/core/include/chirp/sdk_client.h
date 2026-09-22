#pragma once

#include "sdk.h"
#include "proto/chat.pb.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

namespace chirp {
namespace sdk {

// 钩子接口(sdk_client.cc 内部持有,调用方包含对应头文件实现子类)。
class MessageInterceptor;
class AuthProvider;
class MessageStore;
class ChatEventListener;
class CommandHandler;

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

  // ---- 钩子接口注册(契约见 docs/design-notes/sdk_hooks.md):任意线程
  // 可调,须在 Connect() 之前完成;回调全部在 SDK 内部 io 线程触发,引擎
  // 适配层负责派发回游戏线程。
  // 消息拦截:发送前可改写/拦截(OnBeforeSend),接收前可改写/丢弃
  // (OnBeforeReceive)。
  void SetMessageInterceptor(std::shared_ptr<MessageInterceptor> interceptor);
  // 认证提供:Login("") 时经 GetToken() 取 token;登录 AUTH_FAILED 时回调
  // OnTokenExpired 给一次续期重登机会。
  void SetAuthProvider(std::shared_ptr<AuthProvider> provider);
  // 本地消息存储:收到的与发出的消息都会 Save;不设置则完全不落盘。
  void SetMessageStore(std::unique_ptr<MessageStore> store);
  // 生命周期监听器,可注册多个,按注册顺序触发。
  void AddListener(std::shared_ptr<ChatEventListener> listener);
  // 聊天命令处理器('/trade' 等)。注册了至少一个后,"/cmd args" 形态的
  // SendMessage 不再发往服务器而走本地路由;未注册时 '/' 消息照常发送。
  void RegisterCommand(std::unique_ptr<CommandHandler> handler);

  // ---- MessageStore 转发查询:直接访问已注册的存储,可从任意线程调用
  // (自定义 store 的线程安全由实现方负责)。未设置 store 时 LoadHistory
  // 返回空、GetUnreadCount 返回 0、MarkRead/CleanupMessages 为 no-op。
  std::vector<chirp::chat::ChatMessage> LoadHistory(
      chirp::chat::ChannelType type, const std::string& channel_id,
      int limit, int64_t before_timestamp = 0);
  void MarkRead(chirp::chat::ChannelType type, const std::string& channel_id,
                const std::string& message_id);
  int GetUnreadCount(chirp::chat::ChannelType type,
                     const std::string& channel_id);
  void CleanupMessages(int64_t older_than);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace chirp
