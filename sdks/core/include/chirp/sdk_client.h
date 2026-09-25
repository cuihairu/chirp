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

// ---- 便捷 API 的类型化回调:ec 只覆盖传输层(NotConnected/Timeout/Closed/
// Kicked)与协议异常(BadResponse);服务端业务结果(鉴权失败、参数非法、
// 限频、专码等)一律读 resp.code(),ec 为 OK 不代表业务成功。
using SendResponseCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::SendMessageResponse&)>;
using HistoryCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetHistoryResponse&)>;
using MarkReadCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::MarkReadResponse&)>;
using UnreadCountCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetUnreadCountResponse&)>;
using BlockSenderCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::BlockMessageSenderResponse&)>;
using UnblockSenderCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::UnblockMessageSenderResponse&)>;
using BlockedSendersCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetBlockedSendersResponse&)>;
using SetMuteCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::SetChannelMuteResponse&)>;
using ChannelMutesCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetChannelMutesResponse&)>;
using TypingUsersCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetTypingUsersResponse&)>;
using EditMessageCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::EditMessageResponse&)>;
using DeleteMessageCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::DeleteMessageResponse&)>;
using AddReactionCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::AddReactionResponse&)>;
using RemoveReactionCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::RemoveReactionResponse&)>;
using ReactionsCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetReactionsResponse&)>;
using ReadReceiptsCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetReadReceiptsResponse&)>;
using CreateGroupCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::CreateGroupResponse&)>;
using JoinGroupCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::JoinGroupResponse&)>;
using LeaveGroupCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::LeaveGroupResponse&)>;
using InviteToGroupCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::InviteToGroupResponse&)>;
using KickMemberCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::KickMemberResponse&)>;
using GroupInfoCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetGroupInfoResponse&)>;
using GroupMembersCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetGroupMembersResponse&)>;
using UserGroupsCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetUserGroupsResponse&)>;
using MentionSuggestionsCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::GetMentionSuggestionsResponse&)>;
using BulkDeleteCallback =
    std::function<void(const std::error_code& ec, const chirp::chat::BulkDeleteResponse&)>;

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

  // ---- 便捷 API(服务端往返):覆盖消息/已读/黑名单/静音/输入状态/表情/
  // 群组等高频面。回调类型见 sdk.h——ec 只报传输与协议错误,业务结果读
  // resp.code()。与钩子转发方法(LoadHistory/MarkRead/GetUnreadCount)的
  // 区别:钩子系列读写本地存储,Fetch* 系列走服务端请求。任意线程可调。
  // 完整字段用 Request()/OnNotify() 裸口。

  // 扩展发送:群/世界/私聊/引用一条龙。私聊按 (sender, receiver) 归一化
  // channel_id(与服务端一致);channel_id 对非 PRIVATE 必填。带回调——
  // 要 message id 读 resp.message_id(),被拒读 resp.code()(如
  // CONTENT_TOO_LONG/WORD_FILTERED)。同样过拦截器、命令路由与本地存档。
  struct SendOptions {
    chirp::chat::ChannelType channel_type = chirp::chat::PRIVATE;
    std::string channel_id;            // 非 PRIVATE 必填
    std::string receiver_id;           // PRIVATE 必填
    std::string reply_to_message_id;   // 引用的消息 id,空 = 非引用
  };
  void SendMessage(const SendOptions& opts, const std::string& content,
                   SendResponseCallback cb);

  // 服务端历史:按频道拉取,返回服务端权威顺序(与本地 LoadHistory 互补)。
  void FetchHistory(chirp::chat::ChannelType type, const std::string& channel_id,
                    int limit, int64_t before_timestamp, HistoryCallback cb);

  // 已读:把频道读到 message_id(服务端游标 + 未读数来源)。
  void MarkChannelRead(chirp::chat::ChannelType type, const std::string& channel_id,
                       const std::string& message_id, MarkReadCallback cb);
  // 全量未读:总数与按频道明细(服务端聚合)。
  void FetchUnreadCount(UnreadCountCallback cb);

  // 黑名单:拉黑/解除/列表;被拉黑者的消息被服务端静默过滤。
  void BlockUser(const std::string& user_id, BlockSenderCallback cb);
  void UnblockUser(const std::string& user_id, UnblockSenderCallback cb);
  void FetchBlockedUsers(BlockedSendersCallback cb);

  // 频道免打扰(私聊/公会/世界可静音;见服务端 IsMuteableChannel)。
  void SetChannelMute(chirp::chat::ChannelType type, bool muted, SetMuteCallback cb);
  void FetchChannelMutes(ChannelMutesCallback cb);

  // 输入状态:广播"正在输入"(无响应,fire-and-forget);查询当前谁在输入。
  void SendTypingIndicator(chirp::chat::ChannelType type, const std::string& channel_id,
                           bool is_typing);
  void FetchTypingUsers(chirp::chat::ChannelType type, const std::string& channel_id,
                        TypingUsersCallback cb);

  // 消息操作:编辑(作者)/删除(hard_delete 仅管理员语义,透传)/表情回执
  // /已读回执查询。
  void EditMessage(const std::string& message_id, const std::string& content,
                   EditMessageCallback cb);
  void DeleteMessage(const std::string& message_id, bool hard_delete,
                     DeleteMessageCallback cb);
  // 撤回(游戏平面 P0)：发送者在服务端撤回窗口内撤回自己发的消息(默认私聊/公会
  // 2 分钟,`--recall_window_sec` / `--recall_channels` 可调)。回 INVALID_PARAM
  // 表示超窗/该频道不可撤回/已撤回过;非发送者回 AUTH_FAILED。
  void RecallMessage(const std::string& message_id, DeleteMessageCallback cb);
  void AddReaction(const std::string& message_id, const std::string& emoji,
                   AddReactionCallback cb);
  void RemoveReaction(const std::string& message_id, const std::string& emoji,
                      RemoveReactionCallback cb);
  void FetchReactions(const std::string& message_id, const std::string& emoji,
                      ReactionsCallback cb);  // emoji 空 = 全部
  void FetchReadReceipts(const std::string& message_id, ReadReceiptsCallback cb);

  // 批量删除(校验 channel_id 归属)。
  void BulkDeleteMessages(const std::vector<std::string>& message_ids,
                          const std::string& channel_id, BulkDeleteCallback cb);

  // @提及候选(按频道与 query 前缀)。
  void FetchMentionSuggestions(const std::string& channel_id, const std::string& query,
                               MentionSuggestionsCallback cb);

  // 群组:建/进/出/邀/踢/查。
  void CreateGroup(const std::string& group_name, const std::string& description,
                   CreateGroupCallback cb);
  void JoinGroup(const std::string& group_id, JoinGroupCallback cb);
  void LeaveGroup(const std::string& group_id, LeaveGroupCallback cb);
  void InviteToGroup(const std::string& group_id, const std::string& user_id,
                     InviteToGroupCallback cb);
  void KickMember(const std::string& group_id, const std::string& user_id,
                  KickMemberCallback cb);
  void FetchGroupInfo(const std::string& group_id, GroupInfoCallback cb);
  void FetchGroupMembers(const std::string& group_id, int limit, int offset,
                         GroupMembersCallback cb);
  void FetchUserGroups(int limit, int offset, UserGroupsCallback cb);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace chirp
