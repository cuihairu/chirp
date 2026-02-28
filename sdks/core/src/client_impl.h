#ifndef CHIRP_SDK_CLIENT_IMPL_H_
#define CHIRP_SDK_CLIENT_IMPL_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "chirp/core/client.h"
#include "chirp/core/config.h"

// Module implementations
#include "modules/chat/chat_module_impl.h"
#include "modules/social/social_module_impl.h"
#include "modules/voice/voice_module_impl.h"

namespace chirp {
namespace core {

// Client implementation
class ClientImpl : public Client {
public:
  explicit ClientImpl(const Config& config);
  ~ClientImpl() override;

  // Connection management
  bool Connect() override;
  void Disconnect() override;
  bool IsConnected() const override { return connected_; }
  ConnectionState GetConnectionState() const override { return connection_state_; }

  // Authentication
  bool Login(const std::string& user_id, const std::string& token) override;
  void Logout() override;
  std::string GetUserId() const override { return config_.user_id; }
  std::string GetSessionId() const override { return session_id_; }

  // Module access
  ChatModule* GetChatModule() override { return chat_module_.get(); }
  SocialModule* GetSocialModule() override { return social_module_.get(); }
  VoiceModule* GetVoiceModule() override { return voice_module_.get(); }

  // Event callbacks
  void SetConnectionStateCallback(ConnectionStateCallback callback) override {
    connection_state_callback_ = std::move(callback);
  }

private:
  void UpdateConnectionState(ConnectionState new_state, const std::string& reason = "");

  Config config_;
  asio::io_context io_;

  // Thread pool for IO operations
  std::vector<std::thread> io_threads_;
  std::atomic<bool> io_running_;

  // Connection state
  std::atomic<ConnectionState> connection_state_;
  std::atomic<bool> connected_;
  std::string session_id_;

  // Modules
  std::unique_ptr<modules::chat::ChatModuleImpl> chat_module_;
  std::unique_ptr<modules::social::SocialModuleImpl> social_module_;
  std::unique_ptr<modules::voice::VoiceModuleImpl> voice_module_;

  // Event callbacks
  ConnectionStateCallback connection_state_callback_;
};

} // namespace core
} // namespace chirp

#endif // CHIRP_SDK_CLIENT_IMPL_H_
