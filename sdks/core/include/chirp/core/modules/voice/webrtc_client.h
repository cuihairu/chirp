#ifndef CHIRP_CORE_MODULES_VOICE_WEBRTC_CLIENT_H_
#define CHIRP_CORE_MODULES_VOICE_WEBRTC_CLIENT_H_

#include <memory>
#include <string>
#include <functional>
#include <vector>

#include "chirp/core/modules/voice/voice_module.h"

namespace chirp {
namespace core {
namespace modules {
namespace voice {

// Forward declarations for WebRTC types
class PeerConnectionDelegate;
class AudioDeviceModule;
class AudioTrack;
class VideoTrack;

// WebRTC configuration
struct WebRTCConfig {
  // Audio settings
  bool enable_audio = true;
  bool enable_aec = true;           // Acoustic echo cancellation
  bool enable_ns = true;            // Noise suppression
  bool enable_agc = true;           // Automatic gain control
  int audio_sample_rate = 48000;
  int audio_channels = 1;           // Mono for voice

  // Video settings (optional)
  bool enable_video = false;
  int video_width = 1280;
  int video_height = 720;
  int video_fps = 30;

  // Network settings
  std::vector<std::string> stun_servers = {
    "stun:stun.l.google.com:19302",
    "stun:stun1.l.google.com:19302",
  };
  std::vector<std::string> turn_servers;

  // Performance settings
  int receive_audio_channels = 50;  // Max simultaneous audio streams
  bool enable_fec = true;           // Forward error correction
  bool enable_transport_seq_num = true;
};

// Audio device info
struct AudioDeviceInfo {
  std::string id;
  std::string name;
  int channels;
  int sample_rate;
  bool is_default;
  bool is_input;
};

// WebRTC stats
struct WebRTCStats {
  int bytes_sent = 0;
  int bytes_received = 0;
  int packets_sent = 0;
  int packets_received = 0;
  int packets_lost = 0;
  double current_bitrate = 0.0;
  double current_rtt_ms = 0.0;
  double audio_level = 0.0;
};

// WebRTC native client
class WebRTCClient {
public:
  using StatsCallback = std::function<void(const WebRTCStats& stats)>;
  using AudioDeviceCallback = std::function<void(const std::vector<AudioDeviceInfo>& devices)>;

  WebRTCClient();
  ~WebRTCClient();

  // Initialize the WebRTC client
  bool Initialize(const WebRTCConfig& config);

  // Shutdown and cleanup
  void Shutdown();

  // Audio device management
  std::vector<AudioDeviceInfo> GetInputDevices() const;
  std::vector<AudioDeviceInfo> GetOutputDevices() const;
  bool SetInputDevice(const std::string& device_id);
  bool SetOutputDevice(const std::string& device_id);
  std::string GetCurrentInputDevice() const;
  std::string GetCurrentOutputDevice() const;

  // Audio control
  bool SetInputEnabled(bool enabled);
  bool SetOutputEnabled(bool enabled);
  bool IsInputEnabled() const;
  bool IsOutputEnabled() const;

  // Audio level monitoring
  double GetInputAudioLevel() const;
  double GetOutputAudioLevel() const;
  void SetAudioLevelMonitorCallback(std::function<void(double level)> callback);

  // Create peer connection
  std::shared_ptr<webrtc::PeerConnectionInterface> CreatePeerConnection(
      webrtc::PeerConnectionObserver* observer);

  // Create media stream
  std::shared_ptr<webrtc::MediaStreamInterface> CreateLocalMediaStream(
      const std::string& label);

  // Statistics
  void GetStats(std::shared_ptr<webrtc::PeerConnectionInterface> pc,
                StatsCallback callback);

  // Processing
  void ProcessAudioFrame(const void* audio_data, int sample_rate, size_t num_channels, size_t num_frames);
  void SetRemoteAudio(const std::string& track_id, const void* audio_data, int sample_rate, size_t num_channels, size_t num_frames);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Factory for creating WebRTC client
class WebRTCClientFactory {
public:
  static std::unique_ptr<WebRTCClient> Create();
  static void SetLoggingSeverity(int severity);  // 0=verbose, 1=info, 2=warning, 3=error
};

} // namespace voice
} // namespace modules
} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_MODULES_VOICE_WEBRTC_CLIENT_H_
