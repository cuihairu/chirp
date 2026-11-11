#pragma once

#include <string>
#include <string_view>

#include "asr/asr_provider.h"

namespace chirp::voice::asr {

/// @brief Test double: answers every non-empty chunk with the preset
/// transcript; an empty chunk yields "no speech" (empty string). Lets the
/// moderation chain be pinned end to end without an ASR runtime.
class MockAsrProvider : public AsrProvider {
 public:
  explicit MockAsrProvider(std::string transcript);

  std::string Transcribe(std::string_view pcm) override;

 private:
  std::string transcript_;
};

}  // namespace chirp::voice::asr
