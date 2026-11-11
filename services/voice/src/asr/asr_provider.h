#pragma once

#include <string>
#include <string_view>

namespace chirp::voice::asr {

/// @brief Speech-to-text backend interface for the voice moderation chain
/// (VOICE-ASR-FILTER). One call transcribes one PCM chunk (16 kHz, 16-bit
/// signed LE, mono - the whisper.cpp default input shape). An empty return
/// means "no speech recognized in this chunk".
///
/// Implementations must not retain the audio: a chunk is filter input only
/// and is dropped as soon as the transcript comes back (不落原始音频).
/// Threading: intended to be called from the voice io thread only, same
/// single-threaded contract as the word filter downstream.
class AsrProvider {
 public:
  virtual ~AsrProvider() = default;
  virtual std::string Transcribe(std::string_view pcm) = 0;
};

}  // namespace chirp::voice::asr
