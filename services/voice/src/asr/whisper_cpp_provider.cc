#include "asr/whisper_cpp_provider.h"

#include <cstdint>
#include <vector>

#include "logger.h"
#include "whisper.h"

namespace chirp::voice::asr {

struct WhisperCppProvider::Impl {
  explicit Impl(const std::string& model_path) {
    if (model_path.empty()) {
      return;  // disabled: Transcribe will answer "" (fail open)
    }
    whisper_context_params params = whisper_context_default_params();
    ctx = whisper_init_from_file_with_params(model_path.c_str(), nullptr, params);
    if (ctx == nullptr) {
      chirp::common::Logger::Instance().Warn(
          "whisper.cpp failed to load model: " + model_path);
    }
  }
  ~Impl() {
    if (ctx != nullptr) {
      whisper_free(ctx);
    }
  }

  whisper_context* ctx = nullptr;
};

WhisperCppProvider::WhisperCppProvider(std::string model_path)
    : impl_(std::make_unique<Impl>(model_path)) {}

WhisperCppProvider::~WhisperCppProvider() = default;

std::string WhisperCppProvider::Transcribe(std::string_view pcm) {
  if (impl_->ctx == nullptr || pcm.empty()) {
    return {};
  }
  // 16 kHz 16-bit signed LE mono -> float samples in [-1, 1).
  std::vector<float> samples;
  samples.reserve(pcm.size() / 2);
  for (size_t i = 0; i + 1 < pcm.size(); i += 2) {
    const int16_t s = static_cast<int16_t>(static_cast<uint8_t>(pcm[i]) |
                                           (static_cast<uint16_t>(static_cast<uint8_t>(pcm[i + 1])) << 8));
    samples.push_back(static_cast<float>(s) / 32768.0f);
  }
  if (samples.empty()) {
    return {};
  }

  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_realtime = false;
  params.print_progress = false;
  params.print_timestamps = false;
  params.translate = false;
  params.language = "en";  // ASCII default; a non-ASCII value would need setlocale
  if (whisper_full(impl_->ctx, params, samples.data(), static_cast<int>(samples.size())) != 0) {
    chirp::common::Logger::Instance().Warn("whisper.cpp transcription failed");
    return {};
  }

  std::string text;
  const int n_segments = whisper_full_n_segments(impl_->ctx);
  for (int i = 0; i < n_segments; ++i) {
    const char* segment = whisper_full_get_segment_text(impl_->ctx, i);
    if (segment == nullptr) {
      continue;
    }
    if (!text.empty()) {
      text.push_back(' ');
    }
    text += segment;
  }
  return text;
}

}  // namespace chirp::voice::asr
