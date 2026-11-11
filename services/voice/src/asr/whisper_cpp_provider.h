#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "asr/asr_provider.h"

struct whisper_context;

namespace chirp::voice::asr {

/// @brief Local whisper.cpp provider (opt-in: built only when
/// CHIRP_ASR_WHISPER=ON, so the default CI build never needs the whisper
/// runtime). Transcribes 16 kHz 16-bit signed LE mono PCM - the shape the
/// voice moderation chain feeds it.
///
/// Not compiled or tested by this repo's CI; the chain's semantics are
/// pinned through MockAsrProvider. A build with the switch ON needs
/// whisper.h + libwhisper on the toolchain path (whisper.cpp's standard
/// library build).
class WhisperCppProvider : public AsrProvider {
 public:
  // model_path: the ggml-model.bin path (whisper.cpp model file). An empty
  // or unreadable path leaves the provider in a disabled state where
  // Transcribe returns "" (fail open, same spirit as the lexicon filter).
  explicit WhisperCppProvider(std::string model_path);
  ~WhisperCppProvider() override;

  WhisperCppProvider(const WhisperCppProvider&) = delete;
  WhisperCppProvider& operator=(const WhisperCppProvider&) = delete;

  std::string Transcribe(std::string_view pcm) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chirp::voice::asr
