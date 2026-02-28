#ifndef CHIRP_CORE_MODULES_VOICE_VOICE_MODULE_H_
#define CHIRP_CORE_MODULES_VOICE_VOICE_MODULE_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "chirp/core/modules/voice/voice_events.h"

namespace chirp {
namespace core {
namespace modules {
namespace voice {

// Room types
enum class RoomType {
  PEER_TO_PEER,    // 1:1 voice call
  GROUP,           // Group voice chat
  CHANNEL          // Persistent voice channel
};

// Participant state
enum class ParticipantState {
  JOINING,
  CONNECTED,
  MUTED,
  DEAFENED,
  DISCONNECTED
};

// Participant information
struct Participant {
  std::string user_id;
  std::string username;
  ParticipantState state;
  int64_t joined_at;
  bool is_speaking;
};

// Callback types
using CreateRoomCallback = std::function<void(bool success, const std::string& room_id)>;
using JoinRoomCallback = std::function<void(bool success, const std::string& room_id, const std::vector<std::string>& participants)>;
using SimpleCallback = std::function<void(bool success)>;
using RoomInfoCallback = std::function<void(const RoomInfo& info)>;
using ParticipantsCallback = std::function<void(const std::vector<Participant>& participants)>;

// Voice module interface
class VoiceModule {
public:
  virtual ~VoiceModule() = default;

  // Room management
  virtual void CreateRoom(RoomType type,
                         const std::string& name,
                         int32_t max_participants,
                         CreateRoomCallback callback) = 0;

  virtual void JoinRoom(const std::string& room_id,
                       const std::string& sdp_offer,
                       JoinRoomCallback callback) = 0;

  virtual void LeaveRoom(const std::string& room_id,
                        SimpleCallback callback) = 0;

  virtual void GetRoomInfo(const std::string& room_id,
                          RoomInfoCallback callback) = 0;

  virtual void GetCurrentRoom(RoomInfoCallback callback) = 0;

  // Participant management
  virtual void GetParticipants(const std::string& room_id,
                             ParticipantsCallback callback) = 0;

  // Audio control
  virtual void SetMuted(bool muted) = 0;
  virtual void SetDeafened(bool deafened) = 0;
  virtual bool IsMuted() const = 0;
  virtual bool IsDeafened() const = 0;

  // WebRTC signaling (handled internally, but exposed for advanced use)
  virtual void SendIceCandidate(const std::string& room_id,
                               const std::string& to_user_id,
                               const std::string& candidate,
                               const std::string& sdp_mid,
                               int32_t sdp_mline_index) = 0;

  // Event callbacks
  virtual void SetParticipantJoinedCallback(ParticipantJoinedCallback callback) = 0;
  virtual void SetParticipantLeftCallback(ParticipantLeftCallback callback) = 0;
  virtual void SetParticipantStateChangedCallback(ParticipantStateChangedCallback callback) = 0;
  virtual void SetSpeakingCallback(SpeakingCallback callback) = 0;

  // Audio device management
  virtual void SetAudioInputEnabled(bool enabled) = 0;
  virtual void SetAudioOutputEnabled(bool enabled) = 0;
  virtual bool IsAudioInputEnabled() const = 0;
  virtual bool IsAudioOutputEnabled() const = 0;
};

} // namespace voice
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_VOICE_VOICE_MODULE_H_
