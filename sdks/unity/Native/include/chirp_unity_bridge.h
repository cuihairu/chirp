#pragma once

#include <cstdint>

#ifdef _WIN32
  #define CHIRP_UNITY_API __declspec(dllexport)
#else
  #define CHIRP_UNITY_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Core API
// ============================================================================

/// @brief Initialize the Chirp SDK
/// @param config_json JSON string containing configuration
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_Initialize(const char* config_json);

/// @brief Shutdown the Chirp SDK
CHIRP_UNITY_API void Chirp_Shutdown(void);

/// @brief Connect to the Chirp server
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_Connect(void);

/// @brief Disconnect from the Chirp server
CHIRP_UNITY_API void Chirp_Disconnect(void);

/// @brief Check if connected
/// @return 1 if connected, 0 otherwise
CHIRP_UNITY_API int32_t Chirp_IsConnected(void);

/// @brief Login with user ID and token
/// @param user_id User identifier
/// @param token Authentication token
/// @param device_id Device identifier (can be empty)
/// @param platform Platform identifier ("ios", "android", "web", "pc")
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_Login(const char* user_id, const char* token,
                                   const char* device_id, const char* platform);

/// @brief Logout from current session
CHIRP_UNITY_API void Chirp_Logout(void);

/// @brief Get current user ID
/// @param user_id_buf Buffer to receive user ID
/// @param buf_size Size of buffer
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_GetUserId(char* user_id_buf, uint32_t buf_size);

/// @brief Get current session ID
/// @param session_id_buf Buffer to receive session ID
/// @param buf_size Size of buffer
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_GetSessionId(char* session_id_buf, uint32_t buf_size);

// ============================================================================
// Chat API
// ============================================================================

/// @brief Send a text message
/// @param to_user_id Recipient user ID (empty for group chat)
/// @param channel_id Channel ID
/// @param channel_type Channel type (0=PRIVATE, 1=TEAM, 2=GUILD, 3=WORLD)
/// @param content Message content
/// @param callback_id User-provided callback ID for response
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_SendMessage(const char* to_user_id, const char* channel_id,
                                          int32_t channel_type, const char* content,
                                          int32_t callback_id);

/// @brief Get chat history
/// @param channel_id Channel ID
/// @param channel_type Channel type
/// @param before_timestamp Timestamp to query before (0 for latest)
/// @param limit Maximum number of messages
/// @param callback_id User-provided callback ID for response
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_GetHistory(const char* channel_id, int32_t channel_type,
                                         int64_t before_timestamp, int32_t limit,
                                         int32_t callback_id);

/// @brief Mark messages as read
/// @param channel_id Channel ID
/// @param channel_type Channel type
/// @param message_id Message ID to mark as read up to
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_MarkRead(const char* channel_id, int32_t channel_type,
                                       const char* message_id);

/// @brief Get unread message count
/// @param count_buf Buffer to receive count
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_GetUnreadCount(int32_t* count_buf);

// ============================================================================
// Social API
// ============================================================================

/// @brief Send friend request
/// @param user_id User to send request to
/// @param message Optional message
/// @param callback_id User-provided callback ID
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_SendFriendRequest(const char* user_id, const char* message,
                                                int32_t callback_id);

/// @brief Accept friend request
/// @param request_id Request ID to accept
/// @param callback_id User-provided callback ID
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_AcceptFriendRequest(const char* request_id, int32_t callback_id);

/// @brief Remove friend
/// @param user_id Friend user ID to remove
/// @param callback_id User-provided callback ID
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_RemoveFriend(const char* user_id, int32_t callback_id);

/// @brief Get friend list
/// @param callback_id User-provided callback ID
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_GetFriendList(int32_t callback_id);

/// @brief Set presence status
/// @param status Status (0=OFFLINE, 1=ONLINE, 2=AWAY, 3=DND, 4=IN_GAME)
/// @param status_text Optional status message
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_SetPresence(int32_t status, const char* status_text);

// ============================================================================
// Voice API
// ============================================================================

/// @brief Join a voice room
/// @param room_id Room ID to join
/// @param callback_id User-provided callback ID
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_JoinVoiceRoom(const char* room_id, int32_t callback_id);

/// @brief Leave current voice room
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_LeaveVoiceRoom(void);

/// @brief Set microphone muted state
/// @param muted Whether to mute
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_SetMicMuted(int32_t muted);

/// @brief Set speaker muted state
/// @param muted Whether to mute
/// @return 0 on success, negative error code on failure
CHIRP_UNITY_API int32_t Chirp_SetSpeakerMuted(int32_t muted);

/// @brief Check if mic is muted
/// @return 1 if muted, 0 otherwise
CHIRP_UNITY_API int32_t Chirp_IsMicMuted(void);

/// @brief Check if speaker is muted
/// @return 1 if muted, 0 otherwise
CHIRP_UNITY_API int32_t Chirp_IsSpeakerMuted(void);

// ============================================================================
// Callbacks
// ============================================================================

/// @brief Message received callback type
/// @param message_json JSON string containing message data
typedef void (*ChirpMessageCallback)(const char* message_json);

/// @brief Response callback type
/// @param callback_id User-provided callback ID
/// @param success 1 if successful, 0 otherwise
/// @param data_json JSON string containing response data
typedef void (*ChirpResponseCallback)(int32_t callback_id, int32_t success, const char* data_json);

/// @brief Connection state changed callback type
/// @param connected 1 if connected, 0 otherwise
/// @param error_code Error code if disconnected
typedef void (*ChirpConnectionCallback)(int32_t connected, int32_t error_code);

/// @brief Voice event callback type
/// @param event_type Event type ("joined", "left", "participant_joined", etc.)
/// @param data_json JSON string containing event data
typedef void (*ChirpVoiceEventCallback)(const char* event_type, const char* data_json);

/// @brief Set message received callback
CHIRP_UNITY_API void Chirp_SetMessageCallback(ChirpMessageCallback callback);

/// @brief Set response callback
CHIRP_UNITY_API void Chirp_SetResponseCallback(ChirpResponseCallback callback);

/// @brief Set connection state changed callback
CHIRP_UNITY_API void Chirp_SetConnectionCallback(ChirpConnectionCallback callback);

/// @brief Set voice event callback
CHIRP_UNITY_API void Chirp_SetVoiceEventCallback(ChirpVoiceEventCallback callback);

// ============================================================================
// Error Codes
// ============================================================================

enum ChirpErrorCode {
  CHIRP_OK = 0,
  CHIRP_ERROR_UNKNOWN = -1,
  CHIRP_ERROR_NOT_INITIALIZED = -2,
  CHIRP_ERROR_ALREADY_INITIALIZED = -3,
  CHIRP_ERROR_NOT_CONNECTED = -4,
  CHIRP_ERROR_ALREADY_CONNECTED = -5,
  CHIRP_ERROR_INVALID_PARAM = -6,
  CHIRP_ERROR_AUTH_FAILED = -7,
  CHIRP_ERROR_NETWORK = -8,
  CHIRP_ERROR_TIMEOUT = -9,
  CHIRP_ERROR_RATE_LIMITED = -10,
  CHIRP_ERROR_SESSION_EXPIRED = -11,
};

#ifdef __cplusplus
}
#endif
