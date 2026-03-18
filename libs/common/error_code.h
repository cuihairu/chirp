#pragma once

#include <cstdint>

namespace chirp::common {

/// @brief Error codes for Chirp services
enum ErrorCode : int32_t {
  // Success
  OK = 0,

  // General errors (1-99)
  UNKNOWN_ERROR = 1,
  INVALID_ARGUMENT = 2,
  NOT_FOUND = 3,
  ALREADY_EXISTS = 4,
  PERMISSION_DENIED = 5,
  UNAUTHENTICATED = 6,
  RATE_LIMITED = 7,
  SERVICE_UNAVAILABLE = 8,

  // Auth errors (100-199)
  AUTH_INVALID_CREDENTIALS = 100,
  AUTH_TOKEN_EXPIRED = 101,
  AUTH_TOKEN_INVALID = 102,
  AUTH_USER_NOT_FOUND = 103,
  AUTH_USER_DISABLED = 104,
  AUTH_PASSWORD_TOO_WEAK = 105,
  AUTH_PASSWORD_MISMATCH = 106,
  AUTH_ACCOUNT_LOCKED = 107,
  AUTH_SESSION_EXPIRED = 108,
  AUTH_SESSION_INVALID = 109,
  AUTH_REFRESH_TOKEN_INVALID = 110,

  // Registration errors (200-299)
  REGISTRATION_USERNAME_TAKEN = 200,
  REGISTRATION_EMAIL_TAKEN = 201,
  REGISTRATION_INVALID_USERNAME = 202,
  REGISTRATION_INVALID_EMAIL = 203,
  REGISTRATION_INVALID_PASSWORD = 204,
  REGISTRATION_RATE_LIMITED = 205,

  // Chat errors (300-399)
  CHAT_USER_NOT_FOUND = 300,
  CHAT_MESSAGE_NOT_FOUND = 301,
  CHAT_CHANNEL_NOT_FOUND = 302,
  CHAT_PERMISSION_DENIED = 303,
  CHAT_MESSAGE_TOO_LONG = 304,
  CHAT_RATE_LIMITED = 305,

  // Social errors (400-499)
  SOCIAL_USER_NOT_FOUND = 400,
  SOCIAL_ALREADY_FRIENDS = 401,
  SOCIAL_NOT_FRIENDS = 402,
  SOCIAL_FRIEND_REQUEST_PENDING = 403,
  SOCIAL_FRIEND_REQUEST_NOT_FOUND = 404,
  SOCIAL_BLOCKED = 405,

  // Voice errors (500-599)
  VOICE_ROOM_NOT_FOUND = 500,
  VOICE_ROOM_FULL = 501,
  VOICE_ALREADY_IN_ROOM = 502,
  VOICE_NOT_IN_ROOM = 503,
  VOICE_PERMISSION_DENIED = 504,
  VOICE_CONNECTION_FAILED = 505,
};

/// @brief Get error message for error code
inline const char* ErrorMessage(ErrorCode code) {
  switch (code) {
    case OK: return "Success";
    case UNKNOWN_ERROR: return "Unknown error";
    case INVALID_ARGUMENT: return "Invalid argument";
    case NOT_FOUND: return "Not found";
    case ALREADY_EXISTS: return "Already exists";
    case PERMISSION_DENIED: return "Permission denied";
    case UNAUTHENTICATED: return "Unauthenticated";
    case RATE_LIMITED: return "Rate limited";
    case SERVICE_UNAVAILABLE: return "Service unavailable";
    case AUTH_INVALID_CREDENTIALS: return "Invalid credentials";
    case AUTH_TOKEN_EXPIRED: return "Token expired";
    case AUTH_TOKEN_INVALID: return "Invalid token";
    case AUTH_USER_NOT_FOUND: return "User not found";
    case AUTH_USER_DISABLED: return "User disabled";
    case AUTH_PASSWORD_TOO_WEAK: return "Password too weak";
    case AUTH_PASSWORD_MISMATCH: return "Password mismatch";
    case AUTH_ACCOUNT_LOCKED: return "Account locked";
    case AUTH_SESSION_EXPIRED: return "Session expired";
    case AUTH_SESSION_INVALID: return "Invalid session";
    case AUTH_REFRESH_TOKEN_INVALID: return "Invalid refresh token";
    case REGISTRATION_USERNAME_TAKEN: return "Username already taken";
    case REGISTRATION_EMAIL_TAKEN: return "Email already taken";
    case REGISTRATION_INVALID_USERNAME: return "Invalid username";
    case REGISTRATION_INVALID_EMAIL: return "Invalid email";
    case REGISTRATION_INVALID_PASSWORD: return "Invalid password";
    case REGISTRATION_RATE_LIMITED: return "Registration rate limited";
    case CHAT_USER_NOT_FOUND: return "User not found";
    case CHAT_MESSAGE_NOT_FOUND: return "Message not found";
    case CHAT_CHANNEL_NOT_FOUND: return "Channel not found";
    case CHAT_PERMISSION_DENIED: return "Permission denied";
    case CHAT_MESSAGE_TOO_LONG: return "Message too long";
    case CHAT_RATE_LIMITED: return "Chat rate limited";
    case SOCIAL_USER_NOT_FOUND: return "User not found";
    case SOCIAL_ALREADY_FRIENDS: return "Already friends";
    case SOCIAL_NOT_FRIENDS: return "Not friends";
    case SOCIAL_FRIEND_REQUEST_PENDING: return "Friend request pending";
    case SOCIAL_FRIEND_REQUEST_NOT_FOUND: return "Friend request not found";
    case SOCIAL_BLOCKED: return "User blocked";
    case VOICE_ROOM_NOT_FOUND: return "Room not found";
    case VOICE_ROOM_FULL: return "Room full";
    case VOICE_ALREADY_IN_ROOM: return "Already in room";
    case VOICE_NOT_IN_ROOM: return "Not in room";
    case VOICE_PERMISSION_DENIED: return "Permission denied";
    case VOICE_CONNECTION_FAILED: return "Connection failed";
    default: return "Unknown error";
  }
}

} // namespace chirp::common
