#ifndef CHIRP_SDK_MODULES_VOICE_VOICE_MODULE_IMPL_H_
#define CHIRP_SDK_MODULES_VOICE_VOICE_MODULE_IMPL_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "chirp/core/modules/voice/voice_module.h"
#include "chirp/core/modules/voice/webrtc_client.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

namespace chirp {
namespace core {
namespace modules {
namespace voice {

// Voice module implementation
class VoiceModuleImpl : public VoiceModule {
public:
  VoiceModuleImpl(asio::io_context& io,
                  const std::string& host,
                  uint16_t port,
                  bool use_websocket);

  ~VoiceModuleImpl() override;

  // Connection
  bool Connect();
  void Disconnect();
  bool IsConnected() const { return connected_; }

  // Room management
  void CreateRoom(RoomType type,
                 const std::string& name,
                 int32_t max_participants,
                 CreateRoomCallback callback) override;

  void JoinRoom(const std::string& room_id,
               const std::string& sdp_offer,
               JoinRoomCallback callback) override;

  void LeaveRoom(const std::string& room_id,
                SimpleCallback callback) override;

  void GetRoomInfo(const std::string& room_id,
                  RoomInfoCallback callback) override;

  void GetCurrentRoom(RoomInfoCallback callback) override;

  // Participant management
  void GetParticipants(const std::string& room_id,
                      ParticipantsCallback callback) override;

  // Audio control
  void SetMuted(bool muted) override {
    is_muted_ = muted;
  }

  void SetDeafened(bool deafened) override {
    is_deafened_ = deafened;
  }

  bool IsMuted() const override { return is_muted_; }
  bool IsDeafened() const override { return is_deafened_; }

  // WebRTC signaling
  void SendIceCandidate(const std::string& room_id,
                       const std::string& to_user_id,
                       const std::string& candidate,
                       const std::string& sdp_mid,
                       int32_t sdp_mline_index) override;

  // Event callbacks
  void SetParticipantJoinedCallback(ParticipantJoinedCallback callback) override {
    participant_joined_callback_ = std::move(callback);
  }

  void SetParticipantLeftCallback(ParticipantLeftCallback callback) override {
    participant_left_callback_ = std::move(callback);
  }

  void SetParticipantStateChangedCallback(ParticipantStateChangedCallback callback) override {
    participant_state_changed_callback_ = std::move(callback);
  }

  void SetSpeakingCallback(SpeakingCallback callback) override {
    speaking_callback_ = std::move(callback);
  }

  // Audio device management
  void SetAudioInputEnabled(bool enabled) override {
    audio_input_enabled_ = enabled;
  }

  void SetAudioOutputEnabled(bool enabled) override {
    audio_output_enabled_ = enabled;
  }

  bool IsAudioInputEnabled() const override { return audio_input_enabled_; }
  bool IsAudioOutputEnabled() const override { return audio_output_enabled_; }

  // Get current room ID
  std::string GetCurrentRoomId() const { return current_room_id_; }

  // WebRTC integration
  bool InitializeWebRTC(const WebRTCConfig& config);
  WebRTCClient* GetWebRTCClient() { return webrtc_client_.get(); }

private:
  void SendPacket(const chirp::gateway::Packet& pkt);
  void ReceiveLoop();

  chirp::voice::RoomType ConvertRoomType(RoomType type);
  RoomType ConvertRoomType(chirp::voice::RoomType type);
  chirp::voice::ParticipantState ConvertParticipantState(ParticipantState state);
  ParticipantState ConvertParticipantState(chirp::voice::ParticipantState state);
  RoomInfo ConvertRoomInfo(const chirp::voice::ParticipantInfo& info);
  Participant ConvertParticipant(const chirp::voice::ParticipantInfo& info);

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
  std::string current_room_id_;

  std::atomic<int64_t> sequence_;

  // Audio state
  std::atomic<bool> is_muted_;
  std::atomic<bool> is_deafened_;
  std::atomic<bool> audio_input_enabled_;
  std::atomic<bool> audio_output_enabled_;

  // Event callbacks
  ParticipantJoinedCallback participant_joined_callback_;
  ParticipantLeftCallback participant_left_callback_;
  ParticipantStateChangedCallback participant_state_changed_callback_;
  SpeakingCallback speaking_callback_;

  // Receive thread
  std::thread receive_thread_;

  // WebRTC client for audio processing
  std::unique_ptr<WebRTCClient> webrtc_client_;
  bool webrtc_initialized_ = false;
};

} // namespace voice
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_SDK_MODULES_VOICE_VOICE_MODULE_IMPL_H_
