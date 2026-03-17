#include "chirp_unity_bridge.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "chirp/core/sdk.h"
#include "common/logger.h"

namespace {

std::mutex g_callback_mutex;
ChirpMessageCallback g_message_callback = nullptr;
ChirpResponseCallback g_response_callback = nullptr;
ChirpConnectionCallback g_connection_callback = nullptr;
ChirpVoiceEventCallback g_voice_event_callback = nullptr;

chirp::core::Client* g_client = nullptr;
std::string g_user_id;
std::string g_session_id;

// Helper to convert message to JSON
std::string MessageToJson(const chirp::core::ChatMessageData& msg) {
  return R"({"message_id":")" + msg.message_id + R"(","sender_id":")" + msg.sender_id +
         R"(","receiver_id":")" + msg.receiver_id + R"(","channel_id":")" + msg.channel_id +
         R"(","content":")" + msg.content + R"(","timestamp":)" +
         std::to_string(msg.timestamp) + "}";
}

} // namespace

extern "C" {

// ============================================================================
// Core API
// ============================================================================

int32_t Chirp_Initialize(const char* config_json) {
  if (g_client) {
    return CHIRP_ERROR_ALREADY_INITIALIZED;
  }

  try {
    chirp::core::Config config;
    // Parse JSON config or use defaults
    config.gateway_host = "127.0.0.1";
    config.gateway_port = 7000;
    config.app_id = "chirp_unity";

    if (!chirp::core::SDK::Initialize(config)) {
      return CHIRP_ERROR_UNKNOWN;
    }

    g_client = chirp::core::SDK::GetClient();
    if (!g_client) {
      return CHIRP_ERROR_UNKNOWN;
    }

    // Set up connection state callback
    g_client->SetConnectionStateCallback([](chirp::core::ConnectionState state, const std::string& reason) {
      std::lock_guard<std::mutex> lock(g_callback_mutex);
      if (g_connection_callback) {
        int connected = (state == chirp::core::ConnectionState::CONNECTED) ? 1 : 0;
        int error_code = (state == chirp::core::ConnectionState::DISCONNECTED) ? -1 : 0;
        g_connection_callback(connected, error_code);
      }
    });

    // Set up chat message callback
    auto* chat_module = g_client->GetChatModule();
    if (chat_module) {
      chat_module->SetMessageCallback([](const chirp::core::ChatMessageData& msg) {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_message_callback) {
          std::string json = MessageToJson(msg);
          g_message_callback(json.c_str());
        }
      });
    }

    return CHIRP_OK;
  } catch (...) {
    return CHIRP_ERROR_UNKNOWN;
  }
}

void Chirp_Shutdown(void) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  chirp::core::SDK::Shutdown();
  g_client = nullptr;
  g_user_id.clear();
  g_session_id.clear();
}

int32_t Chirp_Connect(void) {
  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  return g_client->Connect() ? CHIRP_OK : CHIRP_ERROR_NETWORK;
}

void Chirp_Disconnect(void) {
  if (g_client) {
    g_client->Disconnect();
  }
}

int32_t Chirp_IsConnected(void) {
  if (!g_client) {
    return 0;
  }
  return g_client->IsConnected() ? 1 : 0;
}

int32_t Chirp_Login(const char* user_id, const char* token,
                   const char* device_id, const char* platform) {
  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  if (!user_id) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  bool success = g_client->Login(std::string(user_id), token ? std::string(token) : "");

  if (success) {
    g_user_id = g_client->GetUserId();
    g_session_id = g_client->GetSessionId();
    return CHIRP_OK;
  }

  return CHIRP_ERROR_AUTH_FAILED;
}

void Chirp_Logout(void) {
  if (g_client) {
    g_client->Logout();
    g_user_id.clear();
    g_session_id.clear();
  }
}

int32_t Chirp_GetUserId(char* user_id_buf, uint32_t buf_size) {
  if (!user_id_buf || buf_size == 0) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  if (g_user_id.empty() && g_client) {
    g_user_id = g_client->GetUserId();
  }

  if (g_user_id.empty()) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  if (g_user_id.size() >= buf_size) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  std::memcpy(user_id_buf, g_user_id.c_str(), g_user_id.size() + 1);
  return CHIRP_OK;
}

int32_t Chirp_GetSessionId(char* session_id_buf, uint32_t buf_size) {
  if (!session_id_buf || buf_size == 0) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  if (g_session_id.empty() && g_client) {
    g_session_id = g_client->GetSessionId();
  }

  if (g_session_id.empty()) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  if (g_session_id.size() >= buf_size) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  std::memcpy(session_id_buf, g_session_id.c_str(), g_session_id.size() + 1);
  return CHIRP_OK;
}

// ============================================================================
// Chat API
// ============================================================================

int32_t Chirp_SendMessage(const char* to_user_id, const char* channel_id,
                          int32_t channel_type, const char* content,
                          int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  if (!content) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  auto* chat_module = g_client->GetChatModule();
  if (!chat_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  bool success = chat_module->SendTextMessage(
    to_user_id ? std::string(to_user_id) : "",
    content
  );

  std::lock_guard<std::mutex> lock(g_callback_mutex);
  if (g_response_callback) {
    g_response_callback(callback_id, success ? 1 : 0, "");
  }

  return success ? CHIRP_OK : CHIRP_ERROR_UNKNOWN;
}

int32_t Chirp_GetHistory(const char* channel_id, int32_t channel_type,
                         int64_t before_timestamp, int32_t limit,
                         int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* chat_module = g_client->GetChatModule();
  if (!chat_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  auto messages = chat_module->GetHistory(
    channel_id ? std::string(channel_id) : "",
    static_cast<chirp::core::ChannelType>(channel_type),
    limit
  );

  // Build JSON array of messages
  std::string json = "[";
  for (size_t i = 0; i < messages.size(); ++i) {
    if (i > 0) json += ",";
    json += MessageToJson(messages[i]);
  }
  json += "]";

  std::lock_guard<std::mutex> lock(g_callback_mutex);
  if (g_response_callback) {
    g_response_callback(callback_id, 1, json.c_str());
  }

  return CHIRP_OK;
}

int32_t Chirp_MarkRead(const char* channel_id, int32_t channel_type,
                       const char* message_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* chat_module = g_client->GetChatModule();
  if (!chat_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  bool success = chat_module->MarkRead(
    channel_id ? std::string(channel_id) : "",
    message_id ? std::string(message_id) : ""
  );

  return success ? CHIRP_OK : CHIRP_ERROR_UNKNOWN;
}

int32_t Chirp_GetUnreadCount(int32_t* count_buf) {
  if (!count_buf) {
    return CHIRP_ERROR_INVALID_PARAM;
  }

  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  auto* chat_module = g_client->GetChatModule();
  if (!chat_module) {
    *count_buf = 0;
    return CHIRP_ERROR_UNKNOWN;
  }

  *count_buf = chat_module->GetUnreadCount();
  return CHIRP_OK;
}

// ============================================================================
// Social API
// ============================================================================

int32_t Chirp_SendFriendRequest(const char* user_id, const char* message, int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* social_module = g_client->GetSocialModule();
  if (!social_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  // TODO: Implement via social module
  return CHIRP_OK;
}

int32_t Chirp_AcceptFriendRequest(const char* request_id, int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* social_module = g_client->GetSocialModule();
  if (!social_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  // TODO: Implement via social module
  return CHIRP_OK;
}

int32_t Chirp_RemoveFriend(const char* user_id, int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* social_module = g_client->GetSocialModule();
  if (!social_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  // TODO: Implement via social module
  return CHIRP_OK;
}

int32_t Chirp_GetFriendList(int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* social_module = g_client->GetSocialModule();
  if (!social_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  // TODO: Implement via social module
  return CHIRP_OK;
}

int32_t Chirp_SetPresence(int32_t status, const char* status_text) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* social_module = g_client->GetSocialModule();
  if (!social_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  // TODO: Implement via social module
  return CHIRP_OK;
}

// ============================================================================
// Voice API
// ============================================================================

int32_t Chirp_JoinVoiceRoom(const char* room_id, int32_t callback_id) {
  if (!g_client || !g_client->IsConnected()) {
    return CHIRP_ERROR_NOT_CONNECTED;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  bool success = voice_module->JoinRoom(room_id ? std::string(room_id) : "");

  std::lock_guard<std::mutex> lock(g_callback_mutex);
  if (g_response_callback) {
    g_response_callback(callback_id, success ? 1 : 0, "");
  }

  return success ? CHIRP_OK : CHIRP_ERROR_UNKNOWN;
}

int32_t Chirp_LeaveVoiceRoom(void) {
  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  bool success = voice_module->LeaveRoom();
  return success ? CHIRP_OK : CHIRP_ERROR_UNKNOWN;
}

int32_t Chirp_SetMicMuted(int32_t muted) {
  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  voice_module->SetMicMuted(muted != 0);
  return CHIRP_OK;
}

int32_t Chirp_SetSpeakerMuted(int32_t muted) {
  if (!g_client) {
    return CHIRP_ERROR_NOT_INITIALIZED;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return CHIRP_ERROR_UNKNOWN;
  }

  voice_module->SetSpeakerMuted(muted != 0);
  return CHIRP_OK;
}

int32_t Chirp_IsMicMuted(void) {
  if (!g_client) {
    return 0;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return 0;
  }

  return voice_module->IsMicMuted() ? 1 : 0;
}

int32_t Chirp_IsSpeakerMuted(void) {
  if (!g_client) {
    return 0;
  }

  auto* voice_module = g_client->GetVoiceModule();
  if (!voice_module) {
    return 0;
  }

  return voice_module->IsSpeakerMuted() ? 1 : 0;
}

// ============================================================================
// Callbacks
// ============================================================================

void Chirp_SetMessageCallback(ChirpMessageCallback callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_message_callback = callback;
}

void Chirp_SetResponseCallback(ChirpResponseCallback callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_response_callback = callback;
}

void Chirp_SetConnectionCallback(ChirpConnectionCallback callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_connection_callback = callback;
}

void Chirp_SetVoiceEventCallback(ChirpVoiceEventCallback callback) {
  std::lock_guard<std::mutex> lock(g_callback_mutex);
  g_voice_event_callback = callback;
}

} // extern "C"
