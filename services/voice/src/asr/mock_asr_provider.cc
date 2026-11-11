#include "asr/mock_asr_provider.h"

namespace chirp::voice::asr {

MockAsrProvider::MockAsrProvider(std::string transcript)
    : transcript_(std::move(transcript)) {}

std::string MockAsrProvider::Transcribe(std::string_view pcm) {
  if (pcm.empty()) {
    return {};
  }
  return transcript_;
}

}  // namespace chirp::voice::asr
