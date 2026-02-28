#include "client_impl.h"

namespace chirp {
namespace core {

ClientImpl::ClientImpl(const Config& config)
    : config_(config),
      io_running_(false),
      connection_state_(ConnectionState::DISCONNECTED),
      connected_(false) {

  // Determine thread pool size
  uint32_t num_threads = config_.thread_pool_size;
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4;
    }
  }

  // Create module instances
  bool use_ws = config_.connection.use_websocket;
  chat_module_ = std::make_unique<modules::chat::ChatModuleImpl>(
      io_,
      config_.connection.chat_host,
      config_.connection.chat_port,
      use_ws);

  social_module_ = std::make_unique<modules::social::SocialModuleImpl>(
      io_,
      config_.connection.social_host,
      config_.connection.social_port,
      use_ws);

  voice_module_ = std::make_unique<modules::voice::VoiceModuleImpl>(
      io_,
      config_.connection.voice_host,
      config_.connection.voice_port,
      use_ws);
}

ClientImpl::~ClientImpl() {
  Disconnect();
}

bool ClientImpl::Connect() {
  if (connected_) {
    return true;
  }

  UpdateConnectionState(ConnectionState::CONNECTING);

  // Start IO threads
  io_running_ = true;
  uint32_t num_threads = config_.thread_pool_size;
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4;
    }
  }

  for (uint32_t i = 0; i < num_threads; ++i) {
    io_threads_.emplace_back([this]() {
      while (io_running_) {
        io_.run_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }

  // Connect modules
  bool chat_connected = chat_module_->Connect();
  bool social_connected = social_module_->Connect();
  bool voice_connected = voice_module_->Connect();

  if (chat_connected && social_connected && voice_connected) {
    connected_ = true;
    UpdateConnectionState(ConnectionState::CONNECTED);
    return true;
  }

  UpdateConnectionState(ConnectionState::DISCONNECTED, "Failed to connect to one or more services");
  return false;
}

void ClientImpl::Disconnect() {
  if (!connected_) {
    return;
  }

  UpdateConnectionState(ConnectionState::DISCONNECTING);

  // Disconnect modules
  if (chat_module_) {
    chat_module_->Disconnect();
  }
  if (social_module_) {
    social_module_->Disconnect();
  }
  if (voice_module_) {
    voice_module_->Disconnect();
  }

  // Stop IO threads
  io_running_ = false;
  io_.stop();

  for (auto& thread : io_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  io_threads_.clear();

  connected_ = false;
  session_id_.clear();

  UpdateConnectionState(ConnectionState::DISCONNECTED);
}

bool ClientImpl::Login(const std::string& user_id, const std::string& token) {
  if (!connected_) {
    return false;
  }

  config_.user_id = user_id;

  // Login to each module
  bool chat_login = chat_module_->Login(user_id, token);
  bool social_login = social_module_->Login(user_id, token);
  // Voice doesn't require login for room operations

  return chat_login && social_login;
}

void ClientImpl::Logout() {
  if (chat_module_) {
    chat_module_->Logout();
  }
  if (social_module_) {
    social_module_->Logout();
  }
  session_id_.clear();
}

void ClientImpl::UpdateConnectionState(ConnectionState new_state, const std::string& reason) {
  connection_state_ = new_state;

  if (connection_state_callback_) {
    connection_state_callback_(new_state, reason);
  }
}

// Factory function
std::unique_ptr<Client> Client::Create(const Config& config) {
  return std::make_unique<ClientImpl>(config);
}

} // namespace core
} // namespace chirp
