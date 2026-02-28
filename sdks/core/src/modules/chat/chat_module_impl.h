#ifndef CHIRP_SDK_MODULES_CHAT_CHAT_MODULE_IMPL_H_
#define CHIRP_SDK_MODULES_CHAT_CHAT_MODULE_IMPL_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "chirp/core/modules/chat/chat_module.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace core {
namespace modules {
namespace chat {

// Pending request tracking
struct PendingRequest {
  int64_t sequence;
  std::function<void(const std::string&)> callback;
  int64_t timeout_ms;
  int64_t created_at;
};

// Chat module implementation
class ChatModuleImpl : public ChatModule {
public:
  ChatModuleImpl(asio::io_context& io,
                 const std::string& host,
                 uint16_t port,
                 bool use_websocket);

  ~ChatModuleImpl() override;

  // Connection
  bool Connect();
  void Disconnect();
  bool IsConnected() const { return connected_; }

  // Authentication
  bool Login(const std::string& user_id, const std::string& token = "");
  void Logout();

  // Message operations
  void SendMessage(const std::string& to_user,
                  MessageType type,
                  const std::string& content,
                  SendMessageCallback callback) override;

  void SendChannelMessage(const std::string& channel_id,
                         ChannelType channel_type,
                         MessageType type,
                         const std::string& content,
                         SendMessageCallback callback) override;

  void GetHistory(const std::string& channel_id,
                 ChannelType channel_type,
                 int64_t before_timestamp,
                 int32_t limit,
                 GetHistoryCallback callback) override;

  void MarkRead(const std::string& channel_id,
               ChannelType channel_type,
               const std::string& message_id) override;

  void GetUnreadCount(std::function<void(int32_t count)> callback) override;

  // Event callbacks
  void SetMessageCallback(MessageCallback callback) override {
    message_callback_ = std::move(callback);
  }

  void SetMessageReadCallback(MessageReadCallback callback) override {
    read_callback_ = std::move(callback);
  }

  void SetTypingCallback(TypingCallback callback) override {
    typing_callback_ = std::move(callback);
  }

  // Group operations
  void CreateGroup(const std::string& name,
                  const std::vector<std::string>& members,
                  std::function<void(const std::string& group_id)> callback) override;

  void JoinGroup(const std::string& group_id,
                std::function<void(bool success)> callback) override;

  void LeaveGroup(const std::string& group_id,
                 std::function<void(bool success)> callback) override;

  void GetGroupMembers(const std::string& group_id,
                      std::function<void(const std::vector<GroupMember>& members)> callback) override;

  // Typing indicator
  void SendTypingIndicator(const std::string& channel_id,
                          ChannelType channel_type,
                          bool is_typing) override;

  // Get user ID
  std::string GetUserId() const { return user_id_; }

private:
  void SendPacket(const chirp::gateway::Packet& pkt);
  void ReceiveLoop();
  void ProcessPendingRequests();

  chirp::chat::MessageType ConvertMessageType(MessageType type);
  MessageType ConvertMessageType(chirp::chat::MessageType type);
  chirp::chat::ChannelType ConvertChannelType(ChannelType type);
  ChannelType ConvertChannelType(chirp::chat::ChannelType type);
  Message ConvertMessage(const chirp::chat::ChatMessage& msg);
  GroupMember ConvertGroupMember(const chirp::chat::GroupMember& member);

  asio::io_context& io_;
  std::string host_;
  uint16_t port_;
  bool use_websocket_;

  std::unique_ptr<chirp::network::TcpClient> tcp_client_;
  std::unique_ptr<chirp::network::WebSocketClient> ws_client_;
  std::shared_ptr<chirp::network::Session> session_;

  std::atomic<bool> connected_;
  std::atomic<bool> receiving_;

  std::string user_id_;
  std::string session_id_;

  std::atomic<int64_t> sequence_;

  std::mutex request_mu_;
  std::unordered_map<int64_t, PendingRequest> pending_requests_;
  std::condition_variable request_cv_;

  // Event callbacks
  MessageCallback message_callback_;
  MessageReadCallback read_callback_;
  TypingCallback typing_callback_;

  // Receive thread
  std::thread receive_thread_;
  std::thread request_thread_;
};

} // namespace chat
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_SDK_MODULES_CHAT_CHAT_MODULE_IMPL_H_
