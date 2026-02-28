#ifndef CHIRP_SDK_MODULES_SOCIAL_SOCIAL_MODULE_IMPL_H_
#define CHIRP_SDK_MODULES_SOCIAL_SOCIAL_MODULE_IMPL_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "chirp/core/modules/social/social_module.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/social.pb.h"

namespace chirp {
namespace core {
namespace modules {
namespace social {

// Social module implementation
class SocialModuleImpl : public SocialModule {
public:
  SocialModuleImpl(asio::io_context& io,
                   const std::string& host,
                   uint16_t port,
                   bool use_websocket);

  ~SocialModuleImpl() override;

  // Connection
  bool Connect();
  void Disconnect();
  bool IsConnected() const { return connected_; }

  // Authentication
  bool Login(const std::string& user_id, const std::string& token = "");

  // Friend management
  void AddFriend(const std::string& user_id,
                const std::string& message,
                AddFriendCallback callback) override;

  void AcceptFriendRequest(const std::string& request_id,
                          SimpleCallback callback) override;

  void DeclineFriendRequest(const std::string& request_id,
                            SimpleCallback callback) override;

  void RemoveFriend(const std::string& user_id,
                   SimpleCallback callback) override;

  void GetFriendList(int32_t limit,
                    int32_t offset,
                    FriendListCallback callback) override;

  void GetPendingRequests(std::function<void(const std::vector<FriendRequest>&)> callback) override;

  // Block management
  void BlockUser(const std::string& user_id,
                SimpleCallback callback) override;

  void UnblockUser(const std::string& user_id,
                  SimpleCallback callback) override;

  void GetBlockedList(std::function<void(const std::vector<std::string>&)> callback) override;

  // Presence management
  void SetPresence(PresenceStatus status,
                  const std::string& status_message,
                  const std::string& game_name) override;

  void GetPresence(const std::vector<std::string>& user_ids,
                  PresenceCallback callback) override;

  void SubscribePresence(const std::vector<std::string>& user_ids) override;
  void UnsubscribePresence(const std::vector<std::string>& user_ids) override;

  // Event callbacks
  void SetPresenceCallback(PresenceChangeCallback callback) override {
    presence_callback_ = std::move(callback);
  }

  void SetFriendRequestCallback(FriendRequestCallback callback) override {
    friend_request_callback_ = std::move(callback);
  }

  void SetFriendAcceptedCallback(FriendAcceptedCallback callback) override {
    friend_accepted_callback_ = std::move(callback);
  }

  void SetFriendRemovedCallback(FriendRemovedCallback callback) override {
    friend_removed_callback_ = std::move(callback);
  }

  // Get user ID
  std::string GetUserId() const { return user_id_; }

private:
  void SendPacket(const chirp::gateway::Packet& pkt);
  void ReceiveLoop();

  chirp::social::PresenceStatus ConvertPresenceStatus(PresenceStatus status);
  PresenceStatus ConvertPresenceStatus(chirp::social::PresenceStatus status);
  Friend ConvertFriend(const chirp::social::FriendInfo& info);
  FriendRequest ConvertFriendRequest(const chirp::social::FriendRequest& req);
  Presence ConvertPresence(const chirp::social::PresenceInfo& info);

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

  // Event callbacks
  PresenceChangeCallback presence_callback_;
  FriendRequestCallback friend_request_callback_;
  FriendAcceptedCallback friend_accepted_callback_;
  FriendRemovedCallback friend_removed_callback_;

  // Receive thread
  std::thread receive_thread_;
};

} // namespace social
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_SDK_MODULES_SOCIAL_SOCIAL_MODULE_IMPL_H_
