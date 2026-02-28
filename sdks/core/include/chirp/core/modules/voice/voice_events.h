#ifndef CHIRP_CORE_MODULES_VOICE_VOICE_EVENTS_H_
#define CHIRP_CORE_MODULES_VOICE_VOICE_EVENTS_H_

#include <functional>
#include <string>
#include <vector>

namespace chirp {
namespace core {
namespace modules {
namespace voice {

// Room information
struct RoomInfo {
  std::string room_id;
  std::string room_name;
  RoomType room_type;
  int32_t max_participants;
  int32_t current_participants;
};

// Event callbacks
using ParticipantJoinedCallback = std::function<void(const std::string& room_id,
                                                    const Participant& participant)>;

using ParticipantLeftCallback = std::function<void(const std::string& room_id,
                                                   const std::string& user_id)>;

using ParticipantStateChangedCallback = std::function<void(const std::string& room_id,
                                                          const std::string& user_id,
                                                          ParticipantState state)>;

using SpeakingCallback = std::function<void(const std::string& room_id,
                                           const std::string& user_id,
                                           bool is_speaking)>;

// Audio frame callback for raw PCM data (if implementing custom audio processing)
using AudioFrameCallback = std::function<void(const std::vector<uint8_t>& audio_data,
                                             int sample_rate,
                                             int channels)>;

} // namespace voice
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_VOICE_VOICE_EVENTS_H_
