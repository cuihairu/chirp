#include "asr/asr_filter.h"

#include <utility>

#include "logger.h"

namespace chirp::voice::asr {

namespace {

std::string ToLower(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// The chat-side options the voice chain always runs under: the word filter
// itself never refuses or rewrites the caller's buffer semantics - the voice
// disposition decides what happens, and the masker is reused only to produce
// the kFlag publishable form.
chirp::chat::WordFilterOptions ToChatOptions(const AsrFilterOptions& options) {
  chirp::chat::WordFilterOptions chat_options;
  chat_options.lexicon_path = options.lexicon_path;
  chat_options.policy = chirp::chat::WordFilterPolicy::kReplace;
  chat_options.replacement = options.replacement;
  chat_options.reload_check_interval_ms = options.reload_check_interval_ms;
  return chat_options;
}

}  // namespace

AsrDisposition AsrDispositionFromString(const std::string& policy) {
  const std::string lowered = ToLower(policy);
  if (lowered == "intercept") {
    return AsrDisposition::kIntercept;
  }
  if (lowered == "mute") {
    return AsrDisposition::kMute;
  }
  return AsrDisposition::kFlag;
}

const char* AsrDispositionToString(AsrDisposition disposition) {
  switch (disposition) {
    case AsrDisposition::kNone:
      return "none";
    case AsrDisposition::kIntercept:
      return "intercept";
    case AsrDisposition::kMute:
      return "mute";
    case AsrDisposition::kFlag:
      return "flag";
  }
  return "flag";
}

AsrFilter::AsrFilter(AsrFilterOptions options, std::unique_ptr<AsrProvider> provider)
    : options_(std::move(options)),
      provider_(std::move(provider)),
      word_filter_(ToChatOptions(options_)),
      disposition_(AsrDispositionFromString(options_.disposition)) {}

AsrFilterResult AsrFilter::Inspect(std::string_view pcm) {
  AsrFilterResult result;
  if (pcm.empty()) {
    return result;  // no audio, no speech, nothing to publish
  }
  const std::string transcript = provider_->Transcribe(pcm);
  if (transcript.empty()) {
    return result;  // recognized no speech
  }

  const int hits = word_filter_.Hits(transcript);
  result.hits = hits;
  if (hits == 0) {
    result.publishable = transcript;
    return result;
  }

  result.disposition = disposition_;
  if (disposition_ == AsrDisposition::kFlag) {
    // Reuse the shared masker for the publishable form instead of keeping a
    // second masking implementation in the voice tree.
    std::string masked = transcript;
    word_filter_.Filter("voice-transcript", &masked);
    result.publishable = std::move(masked);
  }
  // kIntercept/kMute: publishable stays empty - the hit text is dropped.
  chirp::common::Logger::Instance().Warn(
      "voice ASR chain flagged " + std::to_string(hits) + " lexicon hit(s) (" +
      AsrDispositionToString(disposition_) + ")");
  return result;
}

AsrFilterResult AsrFilter::InspectAudio(
    const std::shared_ptr<chirp::voice::direct::VoiceState>& state,
    const std::string& speaker_id, const std::string& room_id, std::string_view pcm) {
  AsrFilterResult result = Inspect(pcm);
  if (result.disposition != AsrDisposition::kNone) {
    RecordAudit(speaker_id, room_id, result.disposition, result.hits);
    if (result.disposition == AsrDisposition::kMute) {
      MuteForViolation(state, room_id, speaker_id);
    }
  }
  return result;
}

void AsrFilter::RecordAudit(const std::string& speaker_id, const std::string& room_id,
                            AsrDisposition disposition, int hits) {
  if (audit_.size() >= options_.audit_capacity) {
    audit_.erase(audit_.begin());  // drop the oldest, keep the trail bounded
  }
  audit_.push_back(AsrAuditEntry{speaker_id, room_id, disposition, hits,
                                 chirp::voice::direct::NowMs()});
}

bool MuteForViolation(const std::shared_ptr<chirp::voice::direct::VoiceState>& state,
                      const std::string& room_id, const std::string& user_id) {
  const auto room = chirp::voice::direct::FindRoom(state, room_id);
  if (!room) {
    return false;
  }
  chirp::voice::ParticipantState derived = chirp::voice::CONNECTED;
  {
    std::lock_guard<std::mutex> lock(room->mu);
    auto it = room->participants.find(user_id);
    if (it == room->participants.end()) {
      return false;
    }
    it->second.set_muted(true);
    it->second.set_state(chirp::voice::direct::DeriveParticipantState(it->second.muted(),
                                                                     it->second.deafened()));
    derived = it->second.state();
  }

  chirp::voice::ParticipantStateChangedNotify notify;
  notify.set_room_id(room_id);
  notify.set_user_id(user_id);
  notify.set_state(derived);
  notify.set_timestamp(chirp::voice::direct::NowMs());
  chirp::voice::direct::BroadcastToRoom(room, chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY,
                                        notify.SerializeAsString(), state);
  return true;
}

}  // namespace chirp::voice::asr
