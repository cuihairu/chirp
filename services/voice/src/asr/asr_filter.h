#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "asr/asr_provider.h"
#include "voice_dispatch.h"
#include "word_filter.h"

namespace chirp::voice::asr {

/// @brief What a lexicon hit does to the speaker (VOICE-ASR-FILTER 处置
/// 策略). Deliberately distinct from chat's replace/reject/record, which
/// rewrite or refuse *messages*: voice must act on the *member*.
enum class AsrDisposition {
  kNone,       // no hit (or chain disabled): transcript passes verbatim
  kFlag,       // 事后标记: masked transcript passes, hit lands in the audit trail
  kIntercept,  // 消息侧拦截: transcript suppressed, hit lands in the audit trail
  kMute,       // 实时 mute: transcript suppressed + speaker muted in the room
};

/// @brief Parses "flag" / "intercept" / "mute" (case-insensitive). Unknown
/// strings fall back to kFlag - the least invasive action that still
/// acknowledges a hit.
AsrDisposition AsrDispositionFromString(const std::string& policy);

/// @brief The canonical spelling ("none" / "flag" / "intercept" / "mute"),
/// for audit dumps and logs.
const char* AsrDispositionToString(AsrDisposition disposition);

struct AsrFilterOptions {
  // Passed through to the shared chat WordFilter (hot reload included).
  // Empty path disables the whole chain: InspectAudio is a pass-through.
  std::string lexicon_path;
  std::string disposition = "flag";
  std::string replacement = "**";
  int64_t reload_check_interval_ms = 5000;
  // Bounded in-memory audit trail (voice has no DB audit face yet). Entries
  // carry speaker/room/time/hit-count metadata only - never transcript
  // content: a transcript is user speech, not something to keep around.
  size_t audit_capacity = 128;
};

struct AsrFilterResult {
  AsrDisposition disposition = AsrDisposition::kNone;
  // The only transcript allowed to leave the pipeline: verbatim when clean,
  // lexicon-masked on kFlag (the shared kReplace masker, not a local copy),
  // empty on kIntercept/kMute - the text never reaches any consumer.
  std::string publishable;
  int hits = 0;
};

struct AsrAuditEntry {
  std::string speaker_id;
  std::string room_id;
  AsrDisposition disposition;
  int hits;
  int64_t at_ms;
};

/// @brief The voice moderation chain: PCM -> ASR transcript -> shared chat
/// word filter -> disposition -> room member state (kMute reuses the
/// SET_MUTE machinery). The audio chunk exists only inside the call.
/// Threading: voice io thread only (the word filter's lazy reload state is
/// unsynchronized by contract).
class AsrFilter {
 public:
  AsrFilter(AsrFilterOptions options, std::unique_ptr<AsrProvider> provider);

  /// @brief Pure text half: transcribe and classify. No room interaction,
  /// no audit - callers that only want a verdict (e.g. live captions) stop
  /// here and must treat publishable as the sole output.
  AsrFilterResult Inspect(std::string_view pcm);

  /// @brief The wired chain: Inspect + audit trail + apply the mute
  /// disposition to the room member state. Called by the media path for
  /// every transcribed chunk of a room member.
  AsrFilterResult InspectAudio(const std::shared_ptr<chirp::voice::direct::VoiceState>& state,
                               const std::string& speaker_id, const std::string& room_id,
                               std::string_view pcm);

  const std::vector<AsrAuditEntry>& audit_log() const { return audit_; }
  const AsrFilterOptions& options() const { return options_; }

 private:
  void RecordAudit(const std::string& speaker_id, const std::string& room_id,
                   AsrDisposition disposition, int hits);

  AsrFilterOptions options_;
  std::unique_ptr<AsrProvider> provider_;
  chirp::chat::WordFilter word_filter_;
  AsrDisposition disposition_ = AsrDisposition::kFlag;
  std::vector<AsrAuditEntry> audit_;
};

/// @brief Server-initiated mute: flips muted=true on the participant (same
/// flip + derived state as the client-driven SET_MUTE) and notifies the
/// whole room *including* the offender - a client SET_MUTE excludes the
/// actor because the RESP confirms it, here the notify is the only signal
/// the offender gets. Returns false when the room or participant is gone.
bool MuteForViolation(const std::shared_ptr<chirp::voice::direct::VoiceState>& state,
                      const std::string& room_id, const std::string& user_id);

}  // namespace chirp::voice::asr
