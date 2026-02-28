#ifndef CHIRP_CORE_CLIENT_H_
#define CHIRP_CORE_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include "chirp/core/config.h"
#include "chirp/core/modules/chat/chat_module.h"
#include "chirp/core/modules/social/social_module.h"
#include "chirp/core/modules/voice/voice_module.h"

namespace chirp {
namespace core {

// Connection state
enum class ConnectionState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  RECONNECTING,
  DISCONNECTING
};

// Connection state callback
using ConnectionStateCallback = std::function<void(ConnectionState state, const std::string& reason)>;

// Main client interface
class Client {
public:
  virtual ~Client() = default;

  // Connection management
  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;
  virtual ConnectionState GetConnectionState() const = 0;

  // Authentication
  virtual bool Login(const std::string& user_id, const std::string& token = "") = 0;
  virtual void Logout() = 0;
  virtual std::string GetUserId() const = 0;
  virtual std::string GetSessionId() const = 0;

  // Module access
  virtual ChatModule* GetChatModule() = 0;
  virtual SocialModule* GetSocialModule() = 0;
  virtual VoiceModule* GetVoiceModule() = 0;

  // Event callbacks
  virtual void SetConnectionStateCallback(ConnectionStateCallback callback) = 0;

  // Create a new client instance
  static std::unique_ptr<Client> Create(const Config& config);
};

} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_CLIENT_H_
